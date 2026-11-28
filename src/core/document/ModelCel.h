#pragma once

#include "core/rig3d/JointLimit.h"
#include "core/rig3d/Model.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mothion::doc {

// The 3D model a model cel shows, as it was imported: the file's name and bytes (kept whole in the project, so it opens
// anywhere) and the model read from them. Never changed once made: cels and undo steps share it.
struct ModelSource
{
    std::string fileName;
    std::vector<std::uint8_t> bytes;
    std::shared_ptr<const rig3d::Model> model;
};

// A 3D model cel's own (see CelProperties::model3d): its model, and what the person set for each of its nodes to pose it
// (one for each node; see rig3d::JointSettings). Its poses are the keys of its Deform channel (see rig3d::Pose); its rest
// is the model as it was made, kept as the cel's Deformer rest so a first key starts from it.
struct ModelRig
{
    std::shared_ptr<const ModelSource> source;
    std::vector<rig3d::JointSettings> joints;

    bool isEmpty() const { return !source || !source->model; }
    const rig3d::Model *model() const { return source ? source->model.get() : nullptr; }
    bool operator==(const ModelRig &o) const { return source == o.source && joints == o.joints; }
};

// The model read from a file's bytes, ready to share; nullptr (and `error` says why) if they are no model.
std::shared_ptr<const ModelSource> makeModelSource(std::string fileName, std::vector<std::uint8_t> bytes,
                                                   std::string *error = nullptr);
// The numbers of the model's rest pose (see rig3d::Pose::toNumbers()); none for an empty rig.
std::vector<double> modelRestPose(const ModelRig &rig);

} // namespace mothion::doc
