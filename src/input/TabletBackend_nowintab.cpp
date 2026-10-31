// Non-Windows platforms (or MOTHION_ENABLE_WINTAB=OFF): there is no backend to choose.

#include "input/TabletBackend.h"

namespace mothion::input {

bool isWinTabSupported() { return false; }
bool isWinTabEnabled() { return false; }
bool setWinTabEnabled(bool) { return false; }

} // namespace mothion::input
