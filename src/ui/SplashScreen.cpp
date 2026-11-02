#include "ui/SplashScreen.h"

#include <QFont>
#include <QFontDatabase>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>

namespace mothion::ui {

namespace {

constexpr QSize kSize(600, 340);
constexpr int kLogoSize = 280;

// The bundled Montserrat Bold (resources/fonts, SIL Open Font License),
// loaded once; the default font if it cannot be loaded.
QFont titleFont()
{
    static const QStringList families =
        QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Montserrat-Bold.ttf")));
    QFont font;
    if (!families.isEmpty())
        font.setFamilies(families);
    font.setWeight(QFont::Bold);
    return font;
}

} // namespace

QPixmap splashPixmap(const QString &version, qreal devicePixelRatio)
{
    QPixmap pixmap(kSize * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);

    QPainter p(&pixmap);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);

    // Fixed colors from the logo's night-sky indigo and blues: the splash
    // shows before the theme matters, and it is the brand.
    QLinearGradient background(0, 0, kSize.width(), kSize.height());
    background.setColorAt(0, QColor(0x1d, 0x15, 0x50));
    background.setColorAt(1, QColor(0x0b, 0x0d, 0x2e));
    p.fillRect(QRect(QPoint(), kSize), background);

    const QRect logo(24, (kSize.height() - kLogoSize) / 2, kLogoSize, kLogoSize);
    QRadialGradient glow(logo.center(), kLogoSize * 0.75);
    glow.setColorAt(0, QColor(0x4b, 0x6a, 0xc4, 90));
    glow.setColorAt(1, QColor(0x4b, 0x6a, 0xc4, 0));
    p.fillRect(QRect(QPoint(), kSize), glow);
    p.setPen(QColor(0x43, 0x3e, 0x9d));
    p.drawRect(QRectF(0.5, 0.5, kSize.width() - 1, kSize.height() - 1));

    p.drawPixmap(logo, QPixmap(QStringLiteral(":/icon/mothion.png")));

    const int textLeft = logo.right() + 24;
    const QRect textArea(textLeft, 0, kSize.width() - textLeft - 28, kSize.height());

    QFont title = titleFont();
    title.setPixelSize(46);
    p.setFont(title);
    p.setPen(QColor(0xee, 0xf1, 0xff));
    p.drawText(textArea.adjusted(0, 0, 0, -kSize.height() / 2 + 6), Qt::AlignLeft | Qt::AlignBottom,
               QStringLiteral("Mothion"));

    QFont subtitle;
    subtitle.setPixelSize(15);
    p.setFont(subtitle);
    p.setPen(QColor(0x8f, 0xa9, 0xe8));
    p.drawText(textArea.adjusted(0, kSize.height() / 2 + 6, 0, 0), Qt::AlignLeft | Qt::AlignTop,
               QStringLiteral("Frame-by-frame 2D animation"));

    QFont small = p.font();
    small.setPixelSize(12);
    p.setFont(small);
    p.setPen(QColor(0x6c, 0x6f, 0xb8));
    p.drawText(textArea.adjusted(0, 0, 0, -20), Qt::AlignLeft | Qt::AlignBottom,
               QStringLiteral("Version %1").arg(version));
    return pixmap;
}

} // namespace mothion::ui
