#pragma once

#include <QWidget>

class QLayout;

namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::ui {

class TimelineView;

// Timeline panel: a header bar (frame stepping and counter, new frame,
// onion skin, zoom) and a cel bar above the TimelineView. The onion skin
// button turns it on and off; its arrow opens the settings (frames back and
// on, their colors, opacity). The cel bar adds, deletes and moves cels and
// sets the blend mode and opacity of the current one.
class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    TimelinePanel(document::DocumentController *document, tools::ToolState *toolState, QWidget *parent = nullptr);

    TimelineView *view() const { return m_view; }

private:
    QLayout *celBar();

    document::DocumentController *m_document;
    TimelineView *m_view;
};

} // namespace mothion::ui
