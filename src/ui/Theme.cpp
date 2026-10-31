#include "ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace mothion::ui {

namespace {

void applyStyleSheet(QApplication &app)
{
    QFile qss(QStringLiteral(":/theme/dark.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
}

} // namespace

void applyDarkTheme(QApplication &app)
{
    // Fusion renders identically on Windows and Linux and honours QPalette fully.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const QColor window(0x2b, 0x2b, 0x2e);
    const QColor base(0x1f, 0x1f, 0x22);
    const QColor text(0xd8, 0xd8, 0xdc);
    const QColor disabledText(0x76, 0x76, 0x7c);
    const QColor accent(0x4f, 0x8c, 0xd6);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabledText);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x36, 0x36, 0x3a));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x45, 0x45, 0x4a));
    p.setColor(QPalette::Midlight, QColor(0x3a, 0x3a, 0x3e));
    p.setColor(QPalette::Mid, QColor(0x26, 0x26, 0x29));
    p.setColor(QPalette::Dark, QColor(0x18, 0x18, 0x1a));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabledText);
    app.setPalette(p);
    applyStyleSheet(app);
}

void applyLightTheme(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(app.style()->standardPalette());
    // The .qss only sets spacing and palette() roles, so it suits both themes.
    applyStyleSheet(app);
}

void applySystemTheme(QApplication &app)
{
    if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light)
        applyLightTheme(app);
    else
        applyDarkTheme(app);
}

} // namespace mothion::ui
