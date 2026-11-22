#include "core/deform/Field.h"

#include "core/deform/BoneField.h"
#include "core/deform/CurveField.h"

namespace mothion::deform {

Field::Linear Field::linearAt(Vec2 p, double h) const
{
    const Vec2 dx = map({p.x + h, p.y}) - map({p.x - h, p.y});
    const Vec2 dy = map({p.x, p.y + h}) - map({p.x, p.y - h});
    return {dx.x / (2 * h), dy.x / (2 * h), dx.y / (2 * h), dy.y / (2 * h)};
}

std::unique_ptr<Field> makeField(doc::DeformerKind kind, const doc::Deformer &deformer, const std::vector<double> &pose)
{
    switch (kind) {
    case doc::DeformerKind::None: break;
    case doc::DeformerKind::Bones: return std::make_unique<BoneField>(deformer, pose);
    case doc::DeformerKind::Curve: return std::make_unique<CurveField>(deformer, pose);
    case doc::DeformerKind::Envelope: return std::make_unique<EnvelopeField>(deformer, pose);
    }
    return nullptr;
}

} // namespace mothion::deform
