#pragma once

#include <QPointF>
#include <QSizeF>
#include <QTransform>

namespace mothion::canvas {

// How the document is shown in the canvas widget: zoom, rotation and pan.
//
// Model: the document center is placed at (viewport center + pan), then the
// document is rotated and scaled around its own center. All "...Around"
// operations keep the document point under the given anchor fixed on screen,
// which is what makes zooming at the cursor feel natural.
class ViewTransform
{
public:
    static constexpr double kMinZoom = 0.05;
    static constexpr double kMaxZoom = 64.0;

    void setDocumentSize(const QSizeF &size) { m_document = size; }
    void setViewportSize(const QSizeF &size) { m_viewport = size; }
    QSizeF documentSize() const { return m_document; }
    QSizeF viewportSize() const { return m_viewport; }
    QPointF viewportCenter() const { return {m_viewport.width() / 2, m_viewport.height() / 2}; }

    double zoom() const { return m_zoom; }
    double rotation() const { return m_rotation; } // degrees, (-180, 180]
    QPointF pan() const { return m_pan; }

    QTransform documentToView() const;
    QTransform viewToDocument() const { return documentToView().inverted(); }
    QPointF mapToDocument(const QPointF &viewPos) const { return viewToDocument().map(viewPos); }

    void panBy(const QPointF &delta) { m_pan += delta; }
    void zoomAround(const QPointF &viewAnchor, double factor);
    void rotateAround(const QPointF &viewAnchor, double degrees);

    // Zoom so the whole document is visible, centered, without rotation.
    void fitToView();
    void resetRotation() { rotateAround(viewportCenter(), -m_rotation); }

private:
    // Re-pans so that `docPoint` appears again at `viewAnchor`.
    void keepAnchored(const QPointF &docPoint, const QPointF &viewAnchor);

    QSizeF m_document{1, 1};
    QSizeF m_viewport{1, 1};
    QPointF m_pan;
    double m_zoom = 1.0;
    double m_rotation = 0.0;
};

} // namespace mothion::canvas
