#pragma once

#include "core/document/Drawing.h"

#include <QCoreApplication>
#include <QString>

#include <array>

namespace mothion::ui {

// The blend modes in the order the menus list them, and their names.
inline constexpr std::array<doc::BlendMode, 28> kBlendModes{doc::BlendMode::Normal, doc::BlendMode::Multiply,
                                                           doc::BlendMode::Screen, doc::BlendMode::Overlay,
                                                           doc::BlendMode::Add,
                                                           doc::BlendMode::Darken,
                                                           doc::BlendMode::Lighten,
                                                           doc::BlendMode::ColorDodge,
                                                           doc::BlendMode::ColorBurn,
                                                           doc::BlendMode::HardLight,
                                                           doc::BlendMode::SoftLight,
                                                           doc::BlendMode::Difference,
                                                           doc::BlendMode::Exclusion,
                                                           doc::BlendMode::LinearBurn,
                                                           doc::BlendMode::Subtract,
                                                           doc::BlendMode::Divide,
                                                           doc::BlendMode::LinearLight,
                                                           doc::BlendMode::VividLight,
                                                           doc::BlendMode::PinLight,
                                                           doc::BlendMode::HardMix,
                                                           doc::BlendMode::LighterColor,
                                                           doc::BlendMode::DarkerColor,
                                                           doc::BlendMode::Hue,
                                                           doc::BlendMode::Saturation,
                                                           doc::BlendMode::Color,
                                                           doc::BlendMode::Luminosity,
                                                           doc::BlendMode::GlowDodge,
                                                           doc::BlendMode::AddGlow};

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
    case doc::BlendMode::LinearBurn: return QCoreApplication::translate("BlendModes", "Linear Burn");
    case doc::BlendMode::Subtract: return QCoreApplication::translate("BlendModes", "Subtract");
    case doc::BlendMode::Divide: return QCoreApplication::translate("BlendModes", "Divide");
    case doc::BlendMode::LinearLight: return QCoreApplication::translate("BlendModes", "Linear Light");
    case doc::BlendMode::VividLight: return QCoreApplication::translate("BlendModes", "Vivid Light");
    case doc::BlendMode::PinLight: return QCoreApplication::translate("BlendModes", "Pin Light");
    case doc::BlendMode::HardMix: return QCoreApplication::translate("BlendModes", "Hard Mix");
    case doc::BlendMode::LighterColor: return QCoreApplication::translate("BlendModes", "Lighter Color");
    case doc::BlendMode::DarkerColor: return QCoreApplication::translate("BlendModes", "Darker Color");
    case doc::BlendMode::Hue: return QCoreApplication::translate("BlendModes", "Hue");
    case doc::BlendMode::Saturation: return QCoreApplication::translate("BlendModes", "Saturation");
    case doc::BlendMode::Color: return QCoreApplication::translate("BlendModes", "Color");
    case doc::BlendMode::Luminosity: return QCoreApplication::translate("BlendModes", "Luminosity");
    case doc::BlendMode::GlowDodge: return QCoreApplication::translate("BlendModes", "Glow Dodge");
    case doc::BlendMode::AddGlow: return QCoreApplication::translate("BlendModes", "Add (Glow)");
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
