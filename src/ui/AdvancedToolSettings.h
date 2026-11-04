#pragma once

#include "tools/ToolState.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QListWidget;
class QStackedWidget;

namespace mothion::ui {

class SliderField;

// "Advanced Tool Settings": the brush options that rarely change, by
// category (a list on the left, the category's settings on the right, the
// hovered setting's help at the bottom). A tool window, not modal: it edits
// ToolState and the active brush live, and follows the active tool.
class AdvancedToolSettings : public QDialog
{
    Q_OBJECT

public:
    explicit AdvancedToolSettings(tools::ToolState *state, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void syncFromState();
    // A copy of the active tool's brush with `change` applied to its style.
    template <typename Change> void editStyle(Change change);
    // `widget` shows `help` in the help box while hovered.
    void setHelp(QWidget *widget, const QString &help);

    tools::ToolState *m_state;
    QLabel *m_brushName;
    QListWidget *m_categories;
    QStackedWidget *m_pages;
    QLabel *m_helpTitle;
    QLabel *m_help;

    // Correction
    QCheckBox *m_sharpCorners;
    SliderField *m_stabilization;
    QCheckBox *m_postCorrection;
    SliderField *m_postLevel;
    QCheckBox *m_bySpeed;
    QCheckBox *m_bezier;
    // Starting and ending
    QCheckBox *m_correctStart;
};

} // namespace mothion::ui
