#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "SDL_compat.h"
#include <opus.h>

#ifdef DEBUG_MIC_AB_CAPTURE
#include <cstdio>
#endif

// MicAudioSender — client-side mic capture + Opus encode + LiSendMicAudioFrame pipeline.
//
// Lifecycle: construct → start() → (worker thread runs) → stop() → destroy.
// Destructor calls stop() automatically if not already called.
//
// start() opens the selected SDL2 capture device (or the system default when
// no device name is provided), creates an Opus encoder, and spawns a worker
// thread that captures 20 ms frames at 48 kHz mono, encodes each frame with
// libopus, and calls LiSendMicAudioFrame() with a monotonically increasing
// sequence number. Returns false on any initialisation failure (no capture
// device, Opus init error, etc.); the object is left clean and stop() is a no-op.
//
// stop() signals the worker to exit, joins it, and tears down all SDL2 and
// Opus resources. Safe to call if start() failed or was never called.
// Idempotent: calling stop() more than once is harmless.
//
// Cross-platform: all capture is via SDL2 audio capture (pull mode). The worker
// thread uses std::thread. No platform-specific code in this class.
//
// NOTE: A live LiStartConnection() session must be established before
// start() is called, because LiSendMicAudioFrame() requires an active
// moonlight-common-c session. Wiring to the session lifecycle is done in C3.
class MicAudioSender
{
public:
    MicAudioSender();
    ~MicAudioSender();

    // Non-copyable, non-movable
    MicAudioSender(const MicAudioSender&) = delete;
    MicAudioSender& operator=(const MicAudioSender&) = delete;

    bool start(const std::string& captureDeviceName = std::string());
    void stop();

#ifdef DEBUG_MIC_AB_CAPTURE
    // armDebugCapture — debug A/B capture trigger (UPSTREAM-PR DEFENSIVE NOTE:
    // entire feature is compiled out unless DEBUG_MIC_AB_CAPTURE is defined at
    // build time; see docs/development/mic-ab-capture.md). Called from the
    // SDL input thread (KeyComboMicABCapture); arms the worker to write
    // pre-encode PCM samples to a WAV file at the configured output dir for
    // kDebugCaptureSeconds. Lock-free: a single std::atomic flag flips the
    // worker into capture mode on its next iteration. Idempotent — calling
    // while a capture is in progress is a no-op (returns false).
    //
    // Returns true if capture started, false if a capture is already in
    // progress or the output dir/file could not be opened.
    bool armDebugCapture();
#endif

private:
    void runWorker();

    SDL_AudioDeviceID  m_CaptureDevice;
    OpusEncoder*       m_Encoder;
    std::thread        m_WorkerThread;
    std::atomic<bool>  m_Stopping;

#ifdef DEBUG_MIC_AB_CAPTURE
    // Debug A/B capture state. All access happens on the worker thread except
    // m_DebugCaptureArmed which is the lock-free arm signal from armDebugCapture().
    // Production builds don't compile this in: zero size, zero branches.
    std::atomic<bool> m_DebugCaptureArmed{false};
    FILE*             m_DebugCaptureFile = nullptr;       // owned by worker
    std::uint32_t     m_DebugCaptureSamplesWritten = 0;   // worker-local counter
#endif
};
