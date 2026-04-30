// MicAudioSender — client-side mic capture + Opus encode + send pipeline.
//
// Wire-format reference: docs/design/WIRE.md section 3 (in the umbrella repo).
//
// Mid-stream toggle of the mic preference is intentionally NOT honoured. The
// streaming session (C3) reads the toggle once at stream start and passes it to
// start(). Users must restart the stream to change the setting.

#include "MicAudioSender.h"

#include <Limelight.h>

#include <algorithm>

namespace {

// --- Audio constants -------------------------------------------------------

constexpr int    kSampleRate   = 48000;
constexpr int    kChannels     = 1;
constexpr int    kFrameSamples = 960;   // 48000 Hz × 20 ms = 960 samples/frame

// kFrameBytes: the exact number of bytes for one 20 ms mono s16 PCM frame.
constexpr size_t kFrameBytes   = (size_t)kFrameSamples * kChannels * sizeof(opus_int16);

// kBufBytes: heap buffer with overflow padding.
//
// SDL2-compat (the SDL2 shim over SDL3) has been observed returning MORE bytes
// than requested from SDL_DequeueAudio(), which causes a stack buffer overrun
// when the PCM frame is stack-allocated. Using a heap buffer with 256 bytes of
// overflow padding absorbs any such over-delivery without corrupting adjacent
// memory. See project SecondBrain landmine: "SDL2-compat — DequeueAudio returns
// smaller chunks than requested" and the open thread in moonlight-mic.md.
constexpr size_t kBufBytes     = kFrameBytes + 256;

// Generously sized Opus output buffer. For VOIP at 48 kbit/s the actual encoded
// speech frame is typically 60–150 bytes, but we must leave room for the
// maximum possible output of opus_encode().
constexpr int    kMaxOpusBytes = 1500;

// --- Encoder configuration notes -------------------------------------------
//
// DELIBERATE SAFETY CHOICES — do not change without careful testing:
//
//   OPUS_APPLICATION_VOIP  (not OPUS_APPLICATION_AUDIO)
//   48 kbit/s bitrate
//   NO OPUS_SET_DTX call
//
// The POC experienced STATUS_STACK_BUFFER_OVERRUN (0xC0000409) crashes at
// consistent fault offsets when OPUS_APPLICATION_AUDIO was used together with
// OPUS_SET_DTX(0) at 64 kbit/s. Root cause was never isolated. To allow the
// rewrite to ship with known-stable configuration, OPUS_APPLICATION_VOIP at
// 48 kbit/s is used and OPUS_SET_DTX is NOT called (it remains at the encoder's
// own default). Any quality improvement — especially re-enabling AUDIO mode or
// DTX — requires a standalone harness to confirm stability under sustained
// speech load before landing here.
//
// Reference: moonlight-mic.md open thread "POC SDL2 fix may have stack-overrun
// bug" and plan question "Encoder configuration sweet spot".
constexpr int kBitrate = 48000;

}  // namespace

MicAudioSender::MicAudioSender()
    : m_CaptureDevice(0),
      m_Encoder(nullptr),
      m_Stopping(false)
{
}

MicAudioSender::~MicAudioSender()
{
    // Defensive: stop() is idempotent; the owner should have already called it,
    // but guard against leaks if destruction happens without an explicit stop().
    stop();
}

bool MicAudioSender::start()
{
    SDL_assert(m_CaptureDevice == 0);
    SDL_assert(m_Encoder == nullptr);
    SDL_assert(!m_WorkerThread.joinable());

    // Init the SDL audio subsystem if it isn't already.
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: SDL_InitSubSystem(AUDIO) failed: %s",
                    SDL_GetError());
        return false;
    }

    // Request: 48 kHz, mono, signed 16-bit, pull mode (callback=nullptr).
    // allowed_changes=0: reject devices that can't honour the exact format,
    // so we know the dequeued bytes are s16 without format conversion.
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = kSampleRate;
    want.format   = AUDIO_S16;
    want.channels = (Uint8)kChannels;
    want.samples  = (Uint16)kFrameSamples;
    want.callback = nullptr;    // pull mode

    m_CaptureDevice = SDL_OpenAudioDevice(
        /*device=*/nullptr, /*iscapture=*/1, &want, &have, /*allowed_changes=*/0);
    if (m_CaptureDevice == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: SDL_OpenAudioDevice(capture) failed: %s",
                    SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    // Create the Opus encoder.
    // See encoder configuration notes in the anonymous namespace above for
    // why OPUS_APPLICATION_VOIP is used and why OPUS_SET_DTX is not called.
    int err = OPUS_OK;
    m_Encoder = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &err);
    if (m_Encoder == nullptr || err != OPUS_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: opus_encoder_create failed: %d", err);
        SDL_CloseAudioDevice(m_CaptureDevice);
        m_CaptureDevice = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    if (opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(kBitrate)) != OPUS_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: OPUS_SET_BITRATE(%d) failed", kBitrate);
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
        SDL_CloseAudioDevice(m_CaptureDevice);
        m_CaptureDevice = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "MicAudioSender: capture device opened (freq=%d fmt=0x%04x ch=%u samples=%u)",
                have.freq, (unsigned)have.format,
                (unsigned)have.channels, (unsigned)have.samples);

    m_Stopping.store(false, std::memory_order_relaxed);

    // Unpause so the device starts filling the SDL queue before the worker
    // first calls SDL_GetQueuedAudioSize().
    SDL_PauseAudioDevice(m_CaptureDevice, 0);

    m_WorkerThread = std::thread(&MicAudioSender::runWorker, this);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "MicAudioSender: started (48 kHz mono, VOIP, 20 ms, %d kbit/s)",
                kBitrate / 1000);
    return true;
}

void MicAudioSender::stop()
{
    // Idempotent: nothing to tear down if start() was never called or already
    // returned false.
    if (!m_WorkerThread.joinable() && m_CaptureDevice == 0 && m_Encoder == nullptr) {
        return;
    }

    m_Stopping.store(true, std::memory_order_relaxed);

    if (m_WorkerThread.joinable()) {
        m_WorkerThread.join();
    }

    if (m_CaptureDevice != 0) {
        SDL_PauseAudioDevice(m_CaptureDevice, 1);
        SDL_CloseAudioDevice(m_CaptureDevice);
        m_CaptureDevice = 0;
    }

    if (m_Encoder != nullptr) {
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "MicAudioSender: stopped");
}

void MicAudioSender::runWorker()
{
    // HEAP-allocated PCM buffer.
    //
    // Stack allocation is the suspected cause of the POC's STATUS_STACK_BUFFER_OVERRUN
    // crash: sdl2-compat has been observed returning MORE bytes than requested from
    // SDL_DequeueAudio(), overflowing a stack frame. The 256-byte overflow padding
    // in kBufBytes absorbs any such over-delivery. Heap allocation also avoids
    // eating thread stack space for the frame buffer.
    std::unique_ptr<uint8_t[]> pcmBuf(new uint8_t[kBufBytes]);
    unsigned char opusBuf[kMaxOpusBytes];
    uint16_t seqNumber = 0;

    while (!m_Stopping.load(std::memory_order_relaxed)) {
        // --- Accumulate a full 20 ms frame across as many SDL_DequeueAudio
        //     calls as needed.
        //
        // sdl2-compat (SDL2 shim over SDL3) returns audio in 960-byte chunks
        // regardless of how much was requested, even when the queue reports
        // ≥ 1920 bytes available. A single dequeue is therefore not sufficient
        // to fill a 1920-byte frame. We loop until full.
        //
        // CRITICAL: the `got` return value is clamped to `to_read` before
        // accumulation. sdl2-compat has been observed delivering MORE bytes
        // than requested; without this clamp the write pointer advances past
        // the buffer boundary, causing heap corruption (or stack overrun when
        // the buffer is stack-allocated).
        size_t bytes_filled = 0;
        while (bytes_filled < kFrameBytes &&
               !m_Stopping.load(std::memory_order_relaxed)) {
            if (SDL_GetQueuedAudioSize(m_CaptureDevice) == 0) {
                SDL_Delay(2);
                continue;
            }
            Uint32 to_read = static_cast<Uint32>(kFrameBytes - bytes_filled);
            Uint32 got = SDL_DequeueAudio(
                m_CaptureDevice,
                pcmBuf.get() + bytes_filled,
                to_read);
            // Clamp: sdl2-compat may return MORE bytes than requested.
            bytes_filled += std::min(static_cast<size_t>(got),
                                     static_cast<size_t>(to_read));
        }

        if (m_Stopping.load(std::memory_order_relaxed)) {
            break;
        }
        if (bytes_filled != kFrameBytes) {
            // Partial frame on a non-stopping exit — shouldn't happen, skip.
            continue;
        }

        // Encode the PCM frame.
        opus_int32 encodedLen = opus_encode(
            m_Encoder,
            reinterpret_cast<const opus_int16*>(pcmBuf.get()),
            kFrameSamples,
            opusBuf,
            static_cast<opus_int32>(sizeof(opusBuf)));

        if (encodedLen < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: opus_encode failed: %d", (int)encodedLen);
            continue;
        }

        // Send the encoded frame. LiSendMicAudioFrame() returns 0 on success
        // and a negative value on error (e.g. no active session, host doesn't
        // support mic). Log the first failure at warn level; subsequent frames
        // will keep looping until stop() is called.
        int sendResult = LiSendMicAudioFrame(
            opusBuf,
            static_cast<int>(encodedLen),
            seqNumber);
        if (sendResult < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: LiSendMicAudioFrame returned %d (seq %u)",
                        sendResult, (unsigned)seqNumber);
        }

        ++seqNumber;
    }
}
