// C4 — Test C: MicAudioSender lifecycle.
//
// Under Windows, the SDL dummy audio driver requires an initialized SDL audio
// subsystem that's available in the headless environment. In a CI context
// without a real audio device, start() will return false (no capture device)
// and the test falls back to construct/destruct verification.
//
// This matches the partial_completion_ok: true allowance in the brief.

#include <QtTest>

// SDL_compat.h must come before SDL.h inclusions via other paths.
#include "../app/SDL_compat.h"
#include "../app/streaming/audio/MicAudioSender.h"

class TstMicLifecycle : public QObject
{
    Q_OBJECT

private slots:
    // -----------------------------------------------------------------------
    // Construct and immediately destruct — no crash, no leak, no hang.
    // -----------------------------------------------------------------------
    void test_constructDestruct()
    {
        MicAudioSender sender;
        // Destructor runs here — must not hang, assert, or crash.
    }

    // -----------------------------------------------------------------------
    // stop() called before start() must be a no-op.
    // -----------------------------------------------------------------------
    void test_stopBeforeStart()
    {
        MicAudioSender sender;
        sender.stop();
    }

    // -----------------------------------------------------------------------
    // start() with dummy driver → sleep → stop() round-trip.
    // If start() returns false (no capture device on this runner), the test
    // skips gracefully. Test C's partial_completion_ok means a skip here
    // is acceptable.
    // -----------------------------------------------------------------------
    void test_startStop()
    {
        // Force SDL2 to use the dummy audio backend so no real microphone
        // is needed. Must be set before SDL_InitSubSystem(SDL_INIT_AUDIO).
        SDL_setenv("SDL_AUDIODRIVER", "dummy", /*overwrite=*/1);

        MicAudioSender sender;
        bool started = sender.start();
        if (!started) {
            QSKIP("SDL audio dummy driver did not open a capture device — "
                  "construct/destruct verified in test_constructDestruct()");
        }

        // Let the worker thread spin for a bit.
        SDL_Delay(150);

        // stop() must join cleanly.
        sender.stop();

        // Idempotency: second stop() must be harmless.
        sender.stop();
    }
};

int runMicLifecycleTests(int argc, char** argv)
{
    TstMicLifecycle test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_mic_lifecycle.moc"
