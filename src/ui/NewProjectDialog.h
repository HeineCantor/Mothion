#pragma once

#include <QColor>
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace mothion::ui {

// What File > New asks: the size of the page (a preset, or any width and height), the frame rate, the length of the
// scene and the color of the paper. It starts from the values it was last accepted with, and from the new-project
// settings the first time.
class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(QWidget *parent = nullptr);

    int pageWidth() const;
    int pageHeight() const;
    int frameRate() const;
    int frames() const;
    // The paper: its color, with no alpha when it is see-through (the alpha is 0 then, and the color is the one it will have
    // again if the box is unchecked).
    QColor paper() const;

    // The page sizes the menu offers, as "name" and size; the last entry is "Custom" with no size.
    struct Preset
    {
        QString name;
        int width;
        int height;
    };
    static QList<Preset> presets();

    void accept() override; // remembers the values

private:
    void presetChosen();
    void sizeEdited();
    void showSummary();
    void showPaper();

    QComboBox *m_preset;
    QSpinBox *m_width;
    QSpinBox *m_height;
    QSpinBox *m_fps;
    QSpinBox *m_frames;
    QPushButton *m_paperButton;
    QCheckBox *m_transparent;
    QLabel *m_summary;
    QColor m_paper;
};

} // namespace mothion::ui
