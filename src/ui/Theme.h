#pragma once

class QApplication;

namespace mothion::ui {

// Applies the provisional dark, compact look: Fusion style + dark palette
// + a small stylesheet (resources/theme/dark.qss). All colors live here and
// in the .qss, so the visual design can be replaced without touching panels.
void applyDarkTheme(QApplication &app);

} // namespace mothion::ui
