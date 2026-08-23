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
#include <cctype>
#include <cstring>
#include <string>

namespace {

// --- Audio constants -------------------------------------------------------

constexpr int    kSampleRate   = 48000;
constexpr int    kChannels     = 1;
constexpr int    kFrameSamples = 960;   // 48000 Hz × 20 ms = 960 samples/frame

// kFrameBytes: the exact number of bytes for one 20 ms mono s16 PCM frame.
constexpr size_t kFrameBytes   = (size_t)kFrameSamples * kChannels * sizeof(opus_int16);

// kBufBytes: PCM frame buffer size.
//
// pcmBuf holds exactly one decoded 20 ms frame after memcpy from the scratch
// buffer. The +256 padding is defense-in-depth only — it is NOT the primary
// safety mechanism against SDL_DequeueAudio over-delivery. The scratch buffer
// (kScratchBytes, allocated in runWorker) is the structural fix: SDL writes
// into scratch[], and memcpy clamps the copy into pcmBuf to at most to_read
// bytes per iteration. See the dequeue loop comment in runWorker().
constexpr size_t kBufBytes     = kFrameBytes + 256;

// kScratchBytes: scratch buffer for SDL_DequeueAudio.
//
// SDL_DequeueAudio writes into this buffer — never directly into pcmBuf. Sized
// generously so no documented or plausible sdl2-compat over-delivery can
// overflow it. After each dequeue, exactly min(got, to_read) bytes are
// memcpy'd into pcmBuf.
constexpr size_t kScratchBytes = kFrameBytes * 2 + 4096;

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
constexpr int kEncoderComplexity = 10;

bool logEncoderCtlResult(const char* operation, int result)
{
    if (result != OPUS_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: %s failed: %d (%s)",
                    operation, result, opus_strerror(result));
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "MicAudioSender: %s succeeded", operation);
    return true;
}

bool configureEncoder(OpusEncoder* encoder, int& effectiveBitrate)
{
    bool success = true;
    success &= logEncoderCtlResult(
        "OPUS_SET_BITRATE(48000)",
        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(kBitrate)));
    success &= logEncoderCtlResult(
        "OPUS_SET_COMPLEXITY(10)",
        opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(kEncoderComplexity)));
    success &= logEncoderCtlResult(
        "OPUS_SET_VBR(1)",
        opus_encoder_ctl(encoder, OPUS_SET_VBR(1)));
    success &= logEncoderCtlResult(
        "OPUS_SET_VBR_CONSTRAINT(0)",
        opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(0)));
    success &= logEncoderCtlResult(
        "OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)",
        opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)));

    effectiveBitrate = 0;
    const int getBitrateResult = opus_encoder_ctl(
        encoder, OPUS_GET_BITRATE(&effectiveBitrate));
    success &= logEncoderCtlResult("OPUS_GET_BITRATE", getBitrateResult);
    if (getBitrateResult == OPUS_OK) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: effective Opus bitrate: %d bit/s",
                    effectiveBitrate);
        if (effectiveBitrate != kBitrate) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: effective Opus bitrate mismatch "
                        "(requested=%d effective=%d)",
                        kBitrate, effectiveBitrate);
            success = false;
        }
    }

    return success;
}

bool containsCaseInsensitive(const std::string& haystack, const char* needle)
{
    const std::string needleString(needle);
    return std::search(
        haystack.begin(), haystack.end(),
        needleString.begin(), needleString.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        }) != haystack.end();
}

void logCaptureDevices()
{
#if SDL_VERSION_ATLEAST(2, 24, 0)
    char* defaultCaptureName = nullptr;
    SDL_AudioSpec defaultCaptureSpec;
    SDL_zero(defaultCaptureSpec);
    if (SDL_GetDefaultAudioInfo(&defaultCaptureName, &defaultCaptureSpec, /*iscapture=*/1) == 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: default capture device: %s "
                    "(freq=%d fmt=0x%04x ch=%u samples=%u)",
                    defaultCaptureName != nullptr ? defaultCaptureName : "<unknown>",
                    defaultCaptureSpec.freq,
                    (unsigned)defaultCaptureSpec.format,
                    (unsigned)defaultCaptureSpec.channels,
                    (unsigned)defaultCaptureSpec.samples);
    }
    else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: SDL_GetDefaultAudioInfo(capture) failed: %s",
                    SDL_GetError());
    }
    if (defaultCaptureName != nullptr) {
        SDL_free(defaultCaptureName);
    }
#endif

    int captureDeviceCount = SDL_GetNumAudioDevices(/*iscapture=*/1);
    if (captureDeviceCount < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: SDL_GetNumAudioDevices(capture) failed: %s",
                    SDL_GetError());
        return;
    }

    for (int i = 0; i < captureDeviceCount; i++) {
        const char* name = SDL_GetAudioDeviceName(i, /*iscapture=*/1);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: capture device[%d]: %s",
                    i,
                    name != nullptr ? name : "<unknown>");
    }
}

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

bool MicAudioSender::start(const std::string& captureDeviceName)
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

    logCaptureDevices();

    std::string effectiveCaptureDeviceName = captureDeviceName;
    if (const char* envCaptureDeviceName = SDL_getenv("MOONLIGHT_MIC_CAPTURE_DEVICE")) {
        if (envCaptureDeviceName[0] != '\0') {
            effectiveCaptureDeviceName = envCaptureDeviceName;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: using MOONLIGHT_MIC_CAPTURE_DEVICE override: %s",
                        effectiveCaptureDeviceName.c_str());
        }
    }

    const char* captureDeviceNamePtr =
        effectiveCaptureDeviceName.empty() ? nullptr : effectiveCaptureDeviceName.c_str();

    if (!effectiveCaptureDeviceName.empty()) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: requested capture device: %s",
                    effectiveCaptureDeviceName.c_str());
        if (containsCaseInsensitive(effectiveCaptureDeviceName, "hands-free") ||
                containsCaseInsensitive(effectiveCaptureDeviceName, "headset")) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: selected capture device appears to be a Bluetooth "
                        "hands-free/headset endpoint; opening it can force local headset "
                        "playback into low-quality call mode");
        }
    }
    else {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: requested capture device: <system default>");
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
        captureDeviceNamePtr, /*iscapture=*/1, &want, &have, /*allowed_changes=*/0);
    if (m_CaptureDevice == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: SDL_OpenAudioDevice(capture, %s) failed: %s",
                    captureDeviceNamePtr != nullptr ? captureDeviceNamePtr : "<system default>",
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

    int effectiveBitrate = 0;
    if (!configureEncoder(m_Encoder, effectiveBitrate)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: refusing to start with incomplete or "
                    "incorrect Opus encoder configuration");
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
        SDL_CloseAudioDevice(m_CaptureDevice);
        m_CaptureDevice = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "MicAudioSender: capture device opened: %s "
                "(freq=%d fmt=0x%04x ch=%u samples=%u)",
                captureDeviceNamePtr != nullptr ? captureDeviceNamePtr : "<system default>",
                have.freq, (unsigned)have.format,
                (unsigned)have.channels, (unsigned)have.samples);

    m_Stopping.store(false, std::memory_order_relaxed);

    // Unpause so the device starts filling the SDL queue before the worker
    // first calls SDL_GetQueuedAudioSize().
    SDL_PauseAudioDevice(m_CaptureDevice, 0);

    m_WorkerThread = std::thread(&MicAudioSender::runWorker, this);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "MicAudioSender: started (48 kHz mono s16, VOIP, 20 ms, "
                "effective bitrate %d bit/s, complexity %d, unconstrained VBR, voice signal)",
                effectiveBitrate, kEncoderComplexity);
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
    // HEAP-allocated PCM buffer and scratch buffer.
    //
    // pcmBuf receives exactly one 20 ms frame of s16 PCM per encode iteration,
    // written via memcpy from scratchBuf — never via SDL_DequeueAudio directly.
    // Invariant: pcmBuf never receives more than to_read bytes per iteration.
    //
    // scratchBuf is the SDL_DequeueAudio target. It is large enough that no
    // documented or plausible sdl2-compat over-delivery can overflow it. This is
    // the structural fix for the heap-buffer-overflow bug where sdl2-compat
    // delivers more bytes than requested, corrupting heap-adjacent objects.
    std::unique_ptr<uint8_t[]> pcmBuf(new uint8_t[kBufBytes]);
    std::unique_ptr<uint8_t[]> scratchBuf(new uint8_t[kScratchBytes]);
    unsigned char opusBuf[kMaxOpusBytes];
    OpusEncoder* const encoder = m_Encoder;
    uint16_t seqNumber = 0;
    bool firstCapturedFrameLogged = false;
    bool firstSentFrameLogged = false;

    SDL_assert(encoder != nullptr);
    SDL_assert(encoder == m_Encoder);

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
            // SDL_DequeueAudio writes into scratchBuf[], never into pcmBuf directly.
            // Invariant: pcmBuf never receives more than to_read bytes per iteration.
            Uint32 got = SDL_DequeueAudio(
                m_CaptureDevice,
                scratchBuf.get(),
                to_read);
            if (got > to_read) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "MicAudioSender: SDL_DequeueAudio over-delivery: "
                            "got=%u to_read=%u (clamped)",
                            (unsigned)got, (unsigned)to_read);
            }
            Uint32 copy_size = std::min(got, to_read);
            std::memcpy(pcmBuf.get() + bytes_filled, scratchBuf.get(), copy_size);
            bytes_filled += copy_size;
        }

        if (m_Stopping.load(std::memory_order_relaxed)) {
            break;
        }
        if (bytes_filled != kFrameBytes) {
            // Partial frame on a non-stopping exit — shouldn't happen, skip.
            continue;
        }

        if (!firstCapturedFrameLogged) {
            firstCapturedFrameLogged = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: captured first 20 ms PCM frame (%u bytes)",
                        (unsigned)kFrameBytes);
        }

        // The capture device was opened with allowed_changes=0, so this buffer
        // contains exactly 960 native-endian signed 16-bit mono samples at 48 kHz.
        SDL_assert(bytes_filled == kFrameBytes);
        const opus_int16* const pcmSamples =
            reinterpret_cast<const opus_int16*>(pcmBuf.get());

        // Encode with the same encoder instance configured in start().
        opus_int32 encodedLen = opus_encode(
            encoder,
            pcmSamples,
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
        else if (!firstSentFrameLogged) {
            firstSentFrameLogged = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: submitted first 0x5510 Opus frame "
                        "(seq=%u opusBytes=%d)",
                        (unsigned)seqNumber, (int)encodedLen);
        }

        ++seqNumber;
    }
}
