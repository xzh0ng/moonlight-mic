#include "session_gate_helper.h"

#include "settings/streamingpreferences.h"
#include <Limelight.h>

bool shouldStartMicSender(const StreamingPreferences* prefs, uint32_t hostFlags)
{
    return shouldStartMicSender(prefs->streamMicToHost, hostFlags);
}

bool shouldStartMicSender(bool toggleEnabled, uint32_t hostFlags)
{
    return toggleEnabled && ((hostFlags & SS_FF_MIC_INPUT) != 0);
}
