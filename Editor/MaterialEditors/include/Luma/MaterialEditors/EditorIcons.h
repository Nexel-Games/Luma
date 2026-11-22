#pragma once

#include <array>
#include <filesystem>

#include "Luma/Core/Types.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Slate/Types.h"

// The material editors' toolbar / preview / details icons: PNGs from the
// Godot editor icons (ThirdParty/godot-icons, MIT) (staged at Content/Editor/MaterialEditor/Icons),
// uploaded once by the host as white silhouettes the UI tints. A missing
// icon falls back to Slate's procedural glyph.

namespace Luma::MaterialEditors {

enum class EditorIcon : u8 {
    Apply, Save, Browse, Home, Palette, LiveUpdate,
    Sphere, Cube, Plane, Cylinder, Mesh,
    Reset, Search,
    Details, Stats,  // panel tab icons
    Count
};

// Full-colour icons (Apply, the preview primitives) are drawn as authored; the
// rest load as white and are tinted by the UI.
bool EditorIconKeepsColor(EditorIcon icon);

// File name of an icon inside the icon directory ("Apply.png").
const char* EditorIconFile(EditorIcon icon);

class EditorIconSet {
public:
    // Loads every icon found in `directory`; returns how many loaded.
    // Monochrome icons are baked in `theme` (e.g. the editor accent).
    u32 Load(Renderer& renderer, const std::filesystem::path& directory,
             Slate::Color theme = Slate::Color::RGB(255, 255, 255)) {
        return Load(renderer, directory, theme, theme);
    }
    // Vertical gradient over each icon's visible rows: `top` -> `bottom`.
    u32 Load(Renderer& renderer, const std::filesystem::path& directory, Slate::Color top, Slate::Color bottom);
    Slate::Color Theme() const { return m_theme; }
    TextureHandle Get(EditorIcon icon) const { return m_textures[static_cast<usize>(icon)]; }
    // Width / height of the icon's image (1 when missing).
    f32 Aspect(EditorIcon icon) const { return m_aspects[static_cast<usize>(icon)]; }

private:
    std::array<TextureHandle, static_cast<usize>(EditorIcon::Count)> m_textures{};
    Slate::Color m_theme = Slate::Color::RGB(255, 255, 255);
    std::array<f32, static_cast<usize>(EditorIcon::Count)> m_aspects = [] {
        std::array<f32, static_cast<usize>(EditorIcon::Count)> a{};
        a.fill(1.0f);
        return a;
    }();
};

}  // namespace Luma::MaterialEditors
