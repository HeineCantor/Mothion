#include "ai/Reference.h"

#include "canvas/FrameRenderer.h"
#include "document/DocumentController.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>

namespace mothion::ai {

namespace {

QString tr(const char *text) { return QCoreApplication::translate("ai::Reference", text); }

} // namespace

Reference makeReference(const document::DocumentController &controller)
{
    using document::DocumentController;
    Reference reference;
    const doc::Document &document = controller.document();
    reference.frame = controller.currentFrame();

    std::vector<DocumentController::LayerRef> chosen = controller.layerSelection();
    if (chosen.empty() && controller.layerChosen() && controller.currentDrawing() && controller.currentLayer() != doc::kNoId)
        chosen.push_back({controller.currentCel(), controller.currentDrawingId(), controller.currentLayer()});
    if (chosen.empty()) {
        reference.problem = tr("Choose the layers to refine in the Layers panel.");
        return reference;
    }

    // Each one must be a layer of what the current frame shows.
    for (const DocumentController::LayerRef &ref : chosen) {
        const doc::Cel *cel = document.cel(ref.cel);
        const doc::Drawing *shown = cel ? cel->drawingAt(reference.frame) : nullptr;
        if (!shown || shown->id != ref.drawing || !document.layer(ref.cel, ref.drawing, ref.layer)) {
            reference.problem = tr("The chosen layers must all be on the current frame.");
            return reference;
        }
    }

    // The layers together, in the order the document draws them (cels bottom to top, layers bottom to top).
    doc::Drawing together;
    for (int i = 0; i < document.celCount(); ++i) {
        const doc::Cel &cel = document.celAt(i);
        const doc::Drawing *drawing = cel.drawingAt(reference.frame);
        if (!drawing)
            continue;
        for (const doc::Layer &layer : drawing->layers) {
            const bool isChosen = std::any_of(chosen.begin(), chosen.end(), [&](const DocumentController::LayerRef &r) {
                return r.cel == cel.id() && r.drawing == drawing->id && r.layer == layer.id;
            });
            if (isChosen && layer.kind == doc::LayerKind::Raster && layer.properties.visible) {
                together.layers.push_back(layer);
                ++reference.layers;
            }
        }
    }
    if (together.layers.empty()) {
        reference.problem = tr("The chosen layers are hidden or have nothing to show.");
        return reference;
    }

    QRect area(0, 0, document.width(), document.height());
    if (controller.hasSelection()) {
        reference.selection = controller.selection();
        area = reference.selection.boundingRect().toAlignedRect().intersected(area);
        if (area.isEmpty()) {
            reference.problem = tr("The selection is outside the page.");
            return reference;
        }
    }
    const QImage drawn = canvas::composeDrawing(together, area, false);
    bool anything = false;
    for (int y = 0; y < drawn.height() && !anything; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(drawn.constScanLine(y));
        for (int x = 0; x < drawn.width(); ++x)
            if (qAlpha(line[x]) != 0) {
                anything = true;
                break;
            }
    }
    if (!anything) {
        reference.problem = tr("There is nothing drawn in this area.");
        return reference;
    }

    // Over the paper (white where it is see-through): that is what the model reads as the sketch.
    const QColor paper = QColor::fromRgba(document.paperColor());
    QImage image(area.size(), QImage::Format_RGB32);
    image.fill(paper.alpha() == 255 ? paper : QColor(Qt::white));
    QPainter p(&image);
    p.drawImage(0, 0, drawn);
    if (!reference.selection.isEmpty()) {
        QPainterPath outside;
        outside.addRect(QRectF(QPointF(0, 0), QSizeF(area.size())));
        p.fillPath(outside.subtracted(reference.selection.translated(-area.topLeft())), Qt::white);
    }
    p.end();
    reference.image = image;
    reference.origin = area.topLeft();
    return reference;
}

} // namespace mothion::ai
