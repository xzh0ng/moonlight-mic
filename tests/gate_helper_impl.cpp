// C4 — test-local implementation of the plain bool overload of shouldStartMicSender.
// This overload does NOT depend on StreamingPreferences (and therefore does NOT
// require QT += qml). The StreamingPreferences* overload lives in
// app/streaming/session_gate_helper.cpp and is only compiled into the main app.

#include "../app/streaming/session_gate_helper.h"
#include <Limelight.h>

bool shouldStartMicSender(bool toggleEnabled, uint32_t hostFlags)
{
    return toggleEnabled && ((hostFlags & SS_FF_MIC_INPUT) != 0);
}
