#pragma once

#include "core/document/Drawing.h"

#include <QCoreApplication>
#include <QString>

#include <array>

namespace mothion::ui {

// The blend modes in the order the menus list them, and their names.
inline constexpr std::array<doc::BlendMode, 5> kBlendModes{doc::BlendMode::Normal, doc::BlendMode::Multiply,
                                                           doc::BlendMode::Screen, doc::BlendMode::Overlay,
                                                           doc::BlendMode::Add};

inline QString blendModeName(doc::BlendMode mode)
{
    switch (mode) {
    case doc::BlendMode::Normal: return QCoreApplication::translate("BlendModes", "Normal");
    case doc::BlendMode::Multiply: return QCoreApplication::translate("BlendModes", "Multiply");
    case doc::BlendMode::Screen: return QCoreApplication::translate("BlendModes", "Screen");
    case doc::BlendMode::Overlay: return QCoreApplication::translate("BlendModes", "Overlay");
    case doc::BlendMode::Add: return QCoreApplication::translate("BlendModes", "Add");
    }
    return {};
}

inline int blendModeIndex(doc::BlendMode mode)
{
    for (std::size_t i = 0; i < kBlendModes.size(); ++i)
        if (kBlendModes[i] == mode)
            return int(i);
    return 0;
}

} // namespace mothion::ui
