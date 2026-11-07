#pragma once

#include "core/document/Drawing.h"

#include <QCoreApplication>
#include <QString>

#include <array>

namespace mothion::ui {

// The blend modes in the order the menus list them, and their names.
inline constexpr std::array<doc::BlendMode, 13> kBlendModes{doc::BlendMode::Normal, doc::BlendMode::Multiply,
                                                           doc::BlendMode::Screen, doc::BlendMode::Overlay,
                                                           doc::BlendMode::Add,
                                                           doc::BlendMode::Darken,
                                                           doc::BlendMode::Lighten,
                                                           doc::BlendMode::ColorDodge,
                                                           doc::BlendMode::ColorBurn,
                                                           doc::BlendMode::HardLight,
                                                           doc::BlendMode::SoftLight,
                                                           doc::BlendMode::Difference,
                                                           doc::BlendMode::Exclusion};

inline QString blendModeName(doc::BlendMode mode)
{
    switch (mode) {
    case doc::BlendMode::Normal: return QCoreApplication::translate("BlendModes", "Normal");
    case doc::BlendMode::Multiply: return QCoreApplication::translate("BlendModes", "Multiply");
    case doc::BlendMode::Screen: return QCoreApplication::translate("BlendModes", "Screen");
    case doc::BlendMode::Overlay: return QCoreApplication::translate("BlendModes", "Overlay");
    case doc::BlendMode::Add: return QCoreApplication::translate("BlendModes", "Add");
    case doc::BlendMode::Darken: return QCoreApplication::translate("BlendModes", "Darken");
    case doc::BlendMode::Lighten: return QCoreApplication::translate("BlendModes", "Lighten");
    case doc::BlendMode::ColorDodge: return QCoreApplication::translate("BlendModes", "Color Dodge");
    case doc::BlendMode::ColorBurn: return QCoreApplication::translate("BlendModes", "Color Burn");
    case doc::BlendMode::HardLight: return QCoreApplication::translate("BlendModes", "Hard Light");
    case doc::BlendMode::SoftLight: return QCoreApplication::translate("BlendModes", "Soft Light");
    case doc::BlendMode::Difference: return QCoreApplication::translate("BlendModes", "Difference");
    case doc::BlendMode::Exclusion: return QCoreApplication::translate("BlendModes", "Exclusion");
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
