#pragma once

#include <cstdint>

// Forward declaration — avoids pulling in the full StreamingPreferences headers
// and their transitive dependencies into the test binary.
class StreamingPreferences;

// C4 — thin testable wrapper around the mic-sender startup gate.
//
// Overload 1 (production): takes the full StreamingPreferences object. Used
// in Session::startConnectionAsync() as a drop-in replacement for the inline
// conditional.
//
// Overload 2 (test): takes just the two boolean inputs, no Qt headers required.
// Used by tst_gate_logic.cpp which cannot link the full StreamingPreferences.
bool shouldStartMicSender(const StreamingPreferences* prefs, uint32_t hostFlags);
bool shouldStartMicSender(bool toggleEnabled, uint32_t hostFlags);
