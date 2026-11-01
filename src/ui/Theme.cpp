#include "ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace mothion::ui {

namespace {

const auto kThemeKey = QStringLiteral("ui/theme");

ThemeChoice g_choice = ThemeChoice::System;
QMetaObject::Connection g_systemSchemeConnection;

void useFusion(QApplication &app)
{
    // Fusion renders identically on Windows and Linux and honours QPalette fully.
    // Set once: replacing the style re-polishes every widget.
    if (app.style()->name().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) != 0)
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
}

void applyStyleSheet(QApplication &app)
{
    if (!app.styleSheet().isEmpty())
        return;
    QFile qss(QStringLiteral(":/theme/dark.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
}

void applySystemScheme(QApplication &app)
{
    if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light)
        applyLightTheme(app);
    else
        applyDarkTheme(app);
}

void applyChoice(QApplication &app, ThemeChoice choice)
{
    g_choice = choice;
    QObject::disconnect(g_systemSchemeConnection);
    switch (choice) {
    case ThemeChoice::Light: applyLightTheme(app); break;
    case ThemeChoice::Dark: applyDarkTheme(app); break;
    case ThemeChoice::System:
        applySystemScheme(app);
        g_systemSchemeConnection = QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                                                    &app, [&app] { applySystemScheme(app); });
        break;
    }
}

} // namespace

void applyDarkTheme(QApplication &app)
{
    useFusion(app);

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
    useFusion(app);

    const QColor window(0xf6, 0xf6, 0xf8);
    const QColor base(Qt::white);
    const QColor text(0x1e, 0x1e, 0x22);
    const QColor disabledText(0xa0, 0xa0, 0xa6);
    const QColor accent(0x2f, 0x6f, 0xc4);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0xee, 0xee, 0xf1));
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabledText);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0xfc, 0xfc, 0xfd));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::black);
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(0xf0, 0xf0, 0xf3));
    p.setColor(QPalette::Mid, QColor(0xd4, 0xd4, 0xd9));
    p.setColor(QPalette::Dark, QColor(0xb4, 0xb4, 0xba));
    p.setColor(QPalette::Shadow, QColor(0x80, 0x80, 0x86));
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabledText);
    app.setPalette(p);
    // The .qss only sets spacing and palette() roles, so it suits both themes.
    applyStyleSheet(app);
}

void setThemeChoice(QApplication &app, ThemeChoice choice)
{
    applyChoice(app, choice);
    QSettings().setValue(kThemeKey, static_cast<int>(choice));
}

ThemeChoice themeChoice()
{
    return g_choice;
}

void applySavedTheme(QApplication &app)
{
    const int saved = QSettings().value(kThemeKey, static_cast<int>(ThemeChoice::System)).toInt();
    const bool known = saved >= static_cast<int>(ThemeChoice::System) && saved <= static_cast<int>(ThemeChoice::Dark);
    applyChoice(app, known ? static_cast<ThemeChoice>(saved) : ThemeChoice::System);
}

} // namespace mothion::ui
