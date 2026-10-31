#pragma once

#include <QWidget>

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

class TimelineView;

// Timeline panel: a header bar (frame stepping and counter, new drawing,
// zoom) above the TimelineView.
class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TimelinePanel(document::DocumentController *document, QWidget *parent = nullptr);

    TimelineView *view() const { return m_view; }

private:
    document::DocumentController *m_document;
    TimelineView *m_view;
};

} // namespace mothion::ui
