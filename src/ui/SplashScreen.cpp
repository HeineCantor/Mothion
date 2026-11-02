#include "ui/SplashScreen.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>

namespace mothion::ui {

namespace {

constexpr QSize kSize(600, 340);
constexpr int kLogoSize = 280;

} // namespace

QPixmap splashPixmap(const QString &version, qreal devicePixelRatio)
{
    QPixmap pixmap(kSize * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);

    QPainter p(&pixmap);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);

    // Fixed colors: the splash shows before the theme matters, and it is the brand.
    QLinearGradient background(0, 0, kSize.width(), kSize.height());
    background.setColorAt(0, QColor(0x2e, 0x2b, 0x3c));
    background.setColorAt(1, QColor(0x17, 0x16, 0x1d));
    p.fillRect(QRect(QPoint(), kSize), background);
    p.setPen(QColor(0x45, 0x40, 0x6a));
    p.drawRect(QRectF(0.5, 0.5, kSize.width() - 1, kSize.height() - 1));

    const QRect logo(24, (kSize.height() - kLogoSize) / 2, kLogoSize, kLogoSize);
    p.drawPixmap(logo, QPixmap(QStringLiteral(":/icon/mothion.png")));

    const int textLeft = logo.right() + 24;
    const QRect textArea(textLeft, 0, kSize.width() - textLeft - 28, kSize.height());

    QFont title = p.font();
    title.setPixelSize(52);
    title.setWeight(QFont::Bold);
    p.setFont(title);
    p.setPen(QColor(0xec, 0xea, 0xf4));
    p.drawText(textArea.adjusted(0, 0, 0, -kSize.height() / 2 + 6), Qt::AlignLeft | Qt::AlignBottom,
               QStringLiteral("Mothion"));

    QFont subtitle = p.font();
    subtitle.setPixelSize(15);
    subtitle.setWeight(QFont::Normal);
    p.setFont(subtitle);
    p.setPen(QColor(0xa9, 0xa4, 0xc6));
    p.drawText(textArea.adjusted(0, kSize.height() / 2 + 6, 0, 0), Qt::AlignLeft | Qt::AlignTop,
               QStringLiteral("Frame-by-frame 2D animation"));

    QFont small = p.font();
    small.setPixelSize(12);
    p.setFont(small);
    p.setPen(QColor(0x76, 0x72, 0x8c));
    p.drawText(textArea.adjusted(0, 0, 0, -20), Qt::AlignLeft | Qt::AlignBottom,
               QStringLiteral("Version %1").arg(version));
    return pixmap;
}

} // namespace mothion::ui
