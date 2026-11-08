#pragma once

#include <QString>

namespace mothion::ai {

// What "Refine with AI" remembers (QSettings, "ai/*"; File > Settings > AI):
//  - where ComfyUI listens, and the workflow file it is given;
//  - the standing prompt and negative: what every request carries, whatever the person types in the window and
//    whatever the workflow says (the workflow's own texts are replaced).
QString defaultServer(); // http://127.0.0.1:8188
// The first workflow, in the folder ComfyUI was installed in.
QString defaultWorkflowPath();
// Where scripts/comfyui/install.* puts ComfyUI (%LOCALAPPDATA%\Mothion\ComfyUI, ~/.local/share/Mothion/ComfyUI).
QString defaultComfyFolder();
QString defaultTaggerWorkflowPath(); // the one that tells a picture's tags
QString defaultSystemPrompt();
QString defaultSystemNegative();

QString comfyFolder();
void setComfyFolder(const QString &folder);
// Extra options for ComfyUI's start (--lowvram, --fp16-vae…), as typed.
QString serverArguments();
void setServerArguments(const QString &arguments);
QString serverUrl();
void setServerUrl(const QString &url);
QString workflowPath();
void setWorkflowPath(const QString &path);
QString taggerWorkflowPath();
void setTaggerWorkflowPath(const QString &path);
QString systemPrompt();
void setSystemPrompt(const QString &text);
QString systemNegative();
void setSystemNegative(const QString &text);
void resetSettings();

// The standing text first, then what was typed, as one prompt (either may be empty).
QString combinedPrompt(const QString &standing, const QString &typed);

} // namespace mothion::ai
