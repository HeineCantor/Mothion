#include "input/TabletBackend.h"

#include <QGuiApplication>

namespace mothion::input {

QString backendDescription()
{
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("windows"))
        return isWinTabEnabled() ? QStringLiteral("Wintab") : QStringLiteral("Windows Ink (WM_POINTER)");
    if (platform == QLatin1String("wayland"))
        return QStringLiteral("Wayland (tablet-unstable-v2)");
    if (platform == QLatin1String("xcb"))
        return QStringLiteral("X11 (XInput2)");
    return platform;
}

} // namespace mothion::input
