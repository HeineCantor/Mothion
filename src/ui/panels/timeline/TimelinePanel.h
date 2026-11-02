#pragma once

#include <QWidget>

namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::ui {

class TimelineView;

// Timeline panel: a header bar (frame stepping and counter, new drawing,
// onion skin, zoom) above the TimelineView. The onion skin button turns it
// on and off; its arrow opens the settings (frames back and on, their
// colors, opacity).
class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    TimelinePanel(document::DocumentController *document, tools::ToolState *toolState, QWidget *parent = nullptr);

    TimelineView *view() const { return m_view; }

private:
    document::DocumentController *m_document;
    TimelineView *m_view;
};

} // namespace mothion::ui
