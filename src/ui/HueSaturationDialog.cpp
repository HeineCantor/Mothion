#include "ui/HueSaturationDialog.h"

#include "canvas/ColorAdjust.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QVBoxLayout>

#include <functional>

namespace mothion::ui {

namespace {

// A strip of colors under a slider, the color at each place of it (0..1 from its left to its right) given by `colorAt`;
// the ends taken in as far as the slider's handle keeps from its own ends, so that a place of the strip is under the
// same place of the slider.
class ColorStrip : public QWidget
{
public:
    ColorStrip(QSlider *slider, std::function<QColor(double)> colorAt, QWidget *parent)
        : QWidget(parent)
        , m_slider(slider)
        , m_colorAt(std::move(colorAt))
    {
        setFixedHeight(8);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStyleOptionSlider option;
        option.initFrom(m_slider);
        option.orientation = Qt::Horizontal;
        const int inset = m_slider->style()->pixelMetric(QStyle::PM_SliderLength, &option, m_slider) / 2;
        const QRect r = rect().adjusted(inset, 0, -inset, 0);
        if (r.width() <= 1)
            return;
        QPainter painter(this);
        for (int x = 0; x < r.width(); ++x)
            painter.fillRect(r.x() + x, r.y(), 1, r.height(), m_colorAt(double(x) / (r.width() - 1)));
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(r.adjusted(0, 0, -1, -1));
    }

private:
    QSlider *m_slider;
    std::function<QColor(double)> m_colorAt;
};

QColor fromArgb(std::uint32_t argb) { return QColor::fromRgb(argb | 0xff000000u); }

// The color wheel's hue at `t` (0..1), full and bright, as straight ARGB.
std::uint32_t wheel(double t)
{
    const QColor c = QColor::fromHsvF(float(std::clamp(t, 0.0, 1.0) * (359.0 / 360.0)), 1.0f, 1.0f);
    return 0xff000000u | (std::uint32_t(c.red()) << 16) | (std::uint32_t(c.green()) << 8) | std::uint32_t(c.blue());
}

// A color of middle saturation and brightness: what the saturation and the value strips show changing.
constexpr std::uint32_t kSample = 0xffcc4c4cu;

} // namespace

HueSaturationDialog::HueSaturationDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Hue/Saturation/Value"));
    setModal(false); // a modal dialog has the window behind it dimmed: its colors would not be seen as they are
    auto *layout = new QVBoxLayout(this);
    auto *grid = new QGridLayout;
    grid->setVerticalSpacing(2);
    layout->addLayout(grid);
    auto add = [&](const QString &label, int minimum, int maximum, const QString &name) {
        const int r = grid->rowCount();
        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(minimum, maximum);
        slider->setObjectName(name + QStringLiteral("Slider"));
        slider->setMinimumWidth(260);
        auto *spin = new QSpinBox(this);
        spin->setRange(minimum, maximum);
        spin->setObjectName(name);
        grid->addWidget(new QLabel(label, this), r, 0);
        grid->addWidget(slider, r, 1);
        grid->addWidget(spin, r, 2);
        connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
        connect(spin, &QSpinBox::valueChanged, this, [this] {
            for (QWidget *strip : m_strips)
                strip->update();
            if (!m_quiet)
                emit changed(hue(), saturation(), value());
        });
        return std::pair{spin, slider};
    };
    auto strip = [&](QSlider *slider, std::function<QColor(double)> colorAt, const QString &name) {
        auto *s = new ColorStrip(slider, std::move(colorAt), this);
        s->setObjectName(name);
        grid->addWidget(s, grid->rowCount(), 1);
        m_strips.push_back(s);
    };
    auto spacer = [&] { grid->setRowMinimumHeight(grid->rowCount(), 10); };

    const auto [hue, hueSlider] = add(tr("Hue"), -180, 180, QStringLiteral("hue"));
    m_hue = hue;
    // The wheel as it is, then as the three settings make it.
    strip(hueSlider, [](double t) { return fromArgb(wheel(t)); }, QStringLiteral("hueStrip"));
    strip(hueSlider,
          [this](double t) {
              return fromArgb(canvas::shiftedColor(wheel(t), {this->hue(), this->saturation(), this->value()}));
          },
          QStringLiteral("hueResultStrip"));
    spacer();
    const auto [saturation, saturationSlider] = add(tr("Saturation"), -100, 100, QStringLiteral("saturation"));
    m_saturation = saturation;
    strip(saturationSlider,
          [this](double t) {
              const int s = int(std::lround(-100 + 200 * t));
              return fromArgb(canvas::shiftedColor(kSample, {this->hue(), s, this->value()}));
          },
          QStringLiteral("saturationStrip"));
    spacer();
    const auto [value, valueSlider] = add(tr("Value"), -100, 100, QStringLiteral("value"));
    m_value = value;
    strip(valueSlider,
          [this](double t) {
              const int v = int(std::lround(-100 + 200 * t));
              return fromArgb(canvas::shiftedColor(kSample, {this->hue(), this->saturation(), v}));
          },
          QStringLiteral("valueStrip"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this] { setValues(0, 0, 0); });
    layout->addSpacing(6);
    layout->addWidget(buttons);
}

int HueSaturationDialog::hue() const { return m_hue->value(); }
int HueSaturationDialog::saturation() const { return m_saturation->value(); }
int HueSaturationDialog::value() const { return m_value->value(); }

void HueSaturationDialog::setValues(int hue, int saturation, int value)
{
    m_quiet = true;
    m_hue->setValue(hue);
    m_saturation->setValue(saturation);
    m_value->setValue(value);
    m_quiet = false;
    emit changed(this->hue(), this->saturation(), this->value());
}

} // namespace mothion::ui
