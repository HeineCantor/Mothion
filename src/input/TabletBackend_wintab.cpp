// Windows only, built when MOTHION_ENABLE_WINTAB is ON. Uses Qt *private* API:
// QNativeInterface::Private::QWindowsApplication (QtGui/private/qguiapplication_p.h).
// Private API carries no compatibility promise between Qt minor versions.

#include "input/TabletBackend.h"

#include <QtGui/private/qguiapplication_p.h>

namespace mothion::input {

namespace {

QNativeInterface::Private::QWindowsApplication *windowsApp()
{
    return qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::Private::QWindowsApplication>()
                   : nullptr;
}

} // namespace

bool isWinTabSupported()
{
    return windowsApp() != nullptr;
}

bool isWinTabEnabled()
{
    auto *app = windowsApp();
    return app && app->isWinTabEnabled();
}

bool setWinTabEnabled(bool enabled)
{
    auto *app = windowsApp();
    // Qt loads wintab32.dll here; this fails if no tablet driver provides it.
    return app && app->setWinTabEnabled(enabled) && app->isWinTabEnabled() == enabled;
}

} // namespace mothion::input
