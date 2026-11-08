#include "ai/AiSettings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace mothion::ai {

namespace {

const QString kServerKey = QStringLiteral("ai/server");
const QString kWorkflowKey = QStringLiteral("ai/workflow");
const QString kFolderKey = QStringLiteral("ai/folder");
const QString kArgumentsKey = QStringLiteral("ai/serverArguments");
const QString kPromptKey = QStringLiteral("ai/systemPrompt");
const QString kNegativeKey = QStringLiteral("ai/systemNegative");

} // namespace

QString defaultServer() { return QStringLiteral("http://127.0.0.1:8188"); }

QString defaultComfyFolder()
{
    // %LOCALAPPDATA% on Windows, ~/.local/share on Linux: the folder the installer scripts use.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("Mothion/ComfyUI"));
}

QString defaultWorkflowPath()
{
    return QDir(comfyFolder()).filePath(QStringLiteral("Mothion/mothion_sketch_refine.json"));
}

QString defaultSystemPrompt() { return QStringLiteral("masterpiece, best quality, highres"); }

QString defaultSystemNegative()
{
    return QStringLiteral("worst quality, low quality, bad anatomy, blurry, watermark, text");
}

QString comfyFolder() { return QSettings().value(kFolderKey, defaultComfyFolder()).toString(); }
void setComfyFolder(const QString &folder) { QSettings().setValue(kFolderKey, folder.trimmed()); }
QString serverArguments() { return QSettings().value(kArgumentsKey).toString(); }
void setServerArguments(const QString &arguments) { QSettings().setValue(kArgumentsKey, arguments); }
QString serverUrl() { return QSettings().value(kServerKey, defaultServer()).toString(); }
void setServerUrl(const QString &url) { QSettings().setValue(kServerKey, url.trimmed()); }
QString workflowPath() { return QSettings().value(kWorkflowKey, defaultWorkflowPath()).toString(); }
void setWorkflowPath(const QString &path) { QSettings().setValue(kWorkflowKey, path.trimmed()); }
QString systemPrompt() { return QSettings().value(kPromptKey, defaultSystemPrompt()).toString(); }
void setSystemPrompt(const QString &text) { QSettings().setValue(kPromptKey, text); }
QString systemNegative() { return QSettings().value(kNegativeKey, defaultSystemNegative()).toString(); }
void setSystemNegative(const QString &text) { QSettings().setValue(kNegativeKey, text); }

void resetSettings()
{
    QSettings settings;
    settings.remove(QStringLiteral("ai"));
}

QString combinedPrompt(const QString &standing, const QString &typed)
{
    const QString a = standing.trimmed();
    const QString b = typed.trimmed();
    if (a.isEmpty() || b.isEmpty())
        return a.isEmpty() ? b : a;
    return a + QStringLiteral(", ") + b;
}

} // namespace mothion::ai
