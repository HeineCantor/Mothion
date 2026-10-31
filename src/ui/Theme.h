#pragma once

class QApplication;

namespace mothion::ui {

// Applies the provisional dark, compact look: Fusion style + dark palette
// + a small stylesheet (resources/theme/dark.qss). All colors live here and
// in the .qss, so the visual design can be replaced without touching panels.
void applyDarkTheme(QApplication &app);

// Same as applyDarkTheme, but with Fusion's light palette.
void applyLightTheme(QApplication &app);

// Picks the light or dark theme from the system color scheme, read once at
// startup (dark when the system does not say). Must run before any widget
// or icon is created: icons take their colors from the palette.
void applySystemTheme(QApplication &app);

} // namespace mothion::ui
