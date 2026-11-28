#include "core/document/ModelCel.h"

#include "core/rig3d/GltfLoader.h"
#include "core/rig3d/Pose.h"

namespace mothion::doc {

std::shared_ptr<const ModelSource> makeModelSource(std::string fileName, std::vector<std::uint8_t> bytes, std::string *error)
{
    std::optional<rig3d::Model> model = rig3d::loadGltf(bytes, error);
    if (!model)
        return nullptr;
    auto source = std::make_shared<ModelSource>();
    source->fileName = std::move(fileName);
    source->bytes = std::move(bytes);
    source->model = std::make_shared<const rig3d::Model>(std::move(*model));
    return source;
}

std::vector<double> modelRestPose(const ModelRig &rig)
{
    const rig3d::Model *model = rig.model();
    return model ? rig3d::Pose::rest(*model).toNumbers() : std::vector<double>{};
}

} // namespace mothion::doc
