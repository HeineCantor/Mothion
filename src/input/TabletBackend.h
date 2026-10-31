#pragma once

#include <QString>

namespace mothion::input {

// Which OS tablet API Qt is using.
//
// Only Windows has a choice: Qt 6 uses Windows Ink (WM_POINTER messages) by
// default; Wintab can be switched on through a *private* Qt interface. That
// code lives alone in TabletBackend_wintab.cpp (built when
// MOTHION_ENABLE_WINTAB is ON on Windows) so it is easy to drop if Qt changes it.
// Must be called after the QApplication exists.

bool isWinTabSupported(); // compiled in and running on Windows
bool isWinTabEnabled();
bool setWinTabEnabled(bool enabled); // false if unsupported or the driver has no wintab32.dll

// Human-readable backend name for the debug overlay, e.g. "Windows Ink",
// "Wintab", "Wayland (tablet-v2)", "X11 (XInput2)".
QString backendDescription();

} // namespace mothion::input
