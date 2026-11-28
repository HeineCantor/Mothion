#pragma once

#include "core/document/ModelCel.h"

#include <QImage>
#include <QSize>

#include <vector>

namespace mothion::canvas {

// A 3D model cel's model in a pose (the numbers of its Deform key, see rig3d::Pose), drawn as the pose's camera sees it
// onto a picture of `size` (the page's): premultiplied ARGB, transparent where the model is not.
//
// The mesh is posed on the CPU (blend shapes, then linear blend skinning: rig3d::posedMeshes) and drawn by OpenGL into
// an offscreen framebuffer (4 samples a pixel), lit by a light from the camera. Where there is no OpenGL (a thread that
// is not the application's, a machine with none, tests; or MOTHION_SOFTWARE_3D=1 in the environment) it is drawn on the
// CPU instead (rig3d::rasterize), which looks the same but for the finer points of the edges.
QImage renderModel(const doc::ModelRig &rig, const std::vector<double> &pose, QSize size);

// Whether the last picture was drawn by OpenGL (false: on the CPU).
bool modelRendererUsedOpenGL();

} // namespace mothion::canvas
