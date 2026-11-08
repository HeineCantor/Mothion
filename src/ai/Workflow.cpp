#include "ai/Workflow.h"

#include <QFile>
#include <QJsonDocument>
#include <QObject>

namespace mothion::ai {

QString findNode(const QJsonObject &workflow, const QString &title)
{
    for (auto it = workflow.begin(); it != workflow.end(); ++it)
        if (it.value().toObject().value(QStringLiteral("_meta")).toObject().value(QStringLiteral("title")).toString() == title)
            return it.key();
    return {};
}

namespace {

// Sets one input of the node; false if the node is not there.
bool setInput(QJsonObject &workflow, const QString &node, const QString &input, const QJsonValue &value)
{
    if (node.isEmpty())
        return false;
    QJsonObject object = workflow.value(node).toObject();
    QJsonObject inputs = object.value(QStringLiteral("inputs")).toObject();
    inputs[input] = value;
    object[QStringLiteral("inputs")] = inputs;
    workflow[node] = object;
    return true;
}

} // namespace

bool fillWorkflow(const QJsonObject &workflow, const WorkflowInputs &inputs, QJsonObject *filled, QString *error,
                  bool needsPrompt)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    QJsonObject result = workflow;
    QStringList mustHave{QStringLiteral("MOTHION_INPUT")};
    if (needsPrompt)
        mustHave << QStringLiteral("MOTHION_PROMPT");
    for (const QString &needed : mustHave)
        if (findNode(workflow, needed).isEmpty())
            return fail(QObject::tr("The workflow has no node titled %1: Mothion cannot tell where to put what it sends. "
                                    "Give the nodes these titles (see scripts/comfyui/README.md).")
                            .arg(needed));
    setInput(result, findNode(workflow, QStringLiteral("MOTHION_INPUT")), QStringLiteral("image"), inputs.image);
    setInput(result, findNode(workflow, QStringLiteral("MOTHION_PROMPT")), QStringLiteral("text"), inputs.prompt);
    setInput(result, findNode(workflow, QStringLiteral("MOTHION_NEGATIVE")), QStringLiteral("text"), inputs.negative);
    setInput(result, findNode(workflow, QStringLiteral("MOTHION_SAMPLER")), QStringLiteral("seed"), double(inputs.seed));
    if (filled)
        *filled = result;
    return true;
}

bool loadWorkflow(const QString &path, QJsonObject *workflow, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QObject::tr("The workflow file %1 cannot be read (Settings > AI says where Mothion looks for it).").arg(path));
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return fail(QObject::tr("The workflow file %1 is not valid: %2").arg(path, parse.errorString()));
    if (document.object().contains(QStringLiteral("nodes")) && document.object().contains(QStringLiteral("links")))
        return fail(QObject::tr("%1 is a workflow as the ComfyUI page saves it. Mothion needs the API format "
                                "(in ComfyUI: Export (API), or Save (API Format)).").arg(path));
    if (workflow)
        *workflow = document.object();
    return true;
}

} // namespace mothion::ai
