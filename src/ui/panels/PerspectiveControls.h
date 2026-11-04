#pragma once

#include "core/document/Drawing.h"
#include "document/DocumentController.h"

#include <QWidget>

#include <functional>
#include <optional>

class QCheckBox;
class QToolButton;

namespace mothion::ui {

// The settings of the current layer when it is a perspective layer (hidden
// otherwise): the plane's turn, tilt and roll, the lens, where its center
// is, the grid's cell and size, its color and horizon. A slider being
// dragged shows the plane live and makes one undo step when let go; every
// other edit is an undo step of its own.
class PerspectiveControls : public QWidget
{
    Q_OBJECT

public:
    explicit PerspectiveControls(document::DocumentController *document, QWidget *parent = nullptr);

private:
    // A value of the grid, as an integer for its slider and spin box.
    struct Field
    {
        std::function<int(const perspective::Grid &)> get;
        std::function<void(perspective::Grid &, int)> set;
    };
    void addField(const QString &label, int minimum, int maximum, const QString &suffix, const QString &name,
                  Field field);
    void sync();
    // The perspective layer shown, if the current layer is one.
    std::optional<document::DocumentController::LayerRef> currentRef() const;
    const doc::Layer *currentLayer() const;
    // `change` applied: live while a slider is dragged (`preview`), else as
    // an undo step (from the properties before the drag, if one went on).
    void edit(const std::function<void(perspective::Grid &)> &change, bool preview);

    document::DocumentController *m_document;
    QToolButton *m_color;
    QCheckBox *m_horizon;
    std::vector<std::function<void(const perspective::Grid &)>> m_syncs; // each field from the grid
    std::optional<doc::LayerProperties> m_before; // the layer's, while a slider is dragged
};

} // namespace mothion::ui
