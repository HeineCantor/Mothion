#pragma once

#include <QColor>

class QApplication;

namespace mothion::ui {

// The provisional compact look: Fusion style + a palette + a small stylesheet
// (resources/theme/dark.qss, which only uses palette() roles). All colors live
// here and in the .qss, so the visual design can be replaced without touching
// panels. Widgets and icons read the palette when they paint, so the theme
// can be switched at run time.
enum class ThemeChoice { System, Light, Dark };

void applyDarkTheme(QApplication &app);

// Same as applyDarkTheme, but with Fusion's light palette.
void applyLightTheme(QApplication &app);

// The selection on the light theme: a transparent grey (laid over what is under it) with the usual dark text, the
// cels' own colors showing through. The dark theme keeps the blue of its palette's highlight.
inline QColor lightSelectionColor() { return QColor(0x70, 0x78, 0x82, 115); }

// Applies the theme and remembers it in QSettings. With System the theme
// follows the system color scheme, also when it changes while running
// (dark when the system does not say).
void setThemeChoice(QApplication &app, ThemeChoice choice);
ThemeChoice themeChoice();

// Applies the theme saved by setThemeChoice (System the first time).
void applySavedTheme(QApplication &app);

} // namespace mothion::ui
