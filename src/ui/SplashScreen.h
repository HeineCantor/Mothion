#pragma once

#include <QPixmap>

namespace mothion::ui {

// The startup splash: a dark rectangle with the logo on the left and the
// name and version on the right. Drawn here from the logo in the resources,
// at `devicePixelRatio` so it stays sharp on HiDPI screens.
QPixmap splashPixmap(const QString &version, qreal devicePixelRatio = 1.0);

} // namespace mothion::ui
