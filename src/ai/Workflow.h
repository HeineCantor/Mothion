#pragma once

#include <QJsonObject>
#include <QString>

namespace mothion::ai {

// A ComfyUI workflow in its API form (the file "Save (API format)" makes, or the ones in scripts/comfyui): an object
// of nodes by number, each with a class_type, its inputs and a title in _meta. Mothion finds the nodes it fills in by
// their title:
//   MOTHION_INPUT     a LoadImage: gets the reference picture
//   MOTHION_PROMPT    a CLIPTextEncode: gets the positive text
//   MOTHION_NEGATIVE  a CLIPTextEncode (optional): gets the negative text
//   MOTHION_SAMPLER   a KSampler (optional): gets a new seed each time
//   MOTHION_OUTPUT    the node that saves the picture (optional: else the first picture the run makes)
//   MOTHION_TAGS      (a workflow that describes a picture) the node whose "tags" or "text" output is the answer
// Everything else in the workflow (the models, the denoise, the ControlNet) stays as the person set it.
struct WorkflowInputs
{
    QString image;    // the name ComfyUI gave the uploaded picture (with its folder, if any)
    QString prompt;
    QString negative;
    qint64 seed = 0;
};

// The node with this title ("" if there is none).
QString findNode(const QJsonObject &workflow, const QString &title);
// The workflow with the inputs put in; false, with `error`, if a node that is needed is missing (the one for the
// prompt is needed only when `needsPrompt`: a workflow that tags a picture has none).
bool fillWorkflow(const QJsonObject &workflow, const WorkflowInputs &inputs, QJsonObject *filled, QString *error,
                  bool needsPrompt = true);
// Reads a workflow file.
bool loadWorkflow(const QString &path, QJsonObject *workflow, QString *error);

} // namespace mothion::ai
