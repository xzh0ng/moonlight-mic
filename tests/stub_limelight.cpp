// C4 — Minimal stub implementations of the Limelight (moonlight-common-c)
// symbols referenced by MicAudioSender.cpp.
//
// Only the symbols actually called from MicAudioSender are stubbed.
// LiSendMicAudioFrame returns 0 (success) so the worker thread cycles cleanly.
// LiGetHostFeatureFlags is NOT called by MicAudioSender; it is only used by
// the Session gate — which is tested via shouldStartMicSender() in isolation.

#include <cstdint>

extern "C" {

// Called from MicAudioSender::runWorker() after each encoded frame.
// Returns 0 (success) so the worker loop keeps running.
int LiSendMicAudioFrame(const unsigned char* /*opusData*/,
                        int /*opusLen*/,
                        uint16_t /*seqNumber*/)
{
    return 0;
}

} // extern "C"
