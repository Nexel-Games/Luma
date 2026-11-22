#pragma once

#include <array>
#include <filesystem>

#include "Luma/Core/Types.h"
#include "Luma/RHI/Renderer.h"

// The material editors' toolbar / preview / details icons: PNGs from the
// Godot editor icons (ThirdParty/godot-icons, MIT) (staged at Content/Editor/MaterialEditor/Icons),
// uploaded once by the host as white silhouettes the UI tints. A missing
// icon falls back to Slate's procedural glyph.

namespace Luma::MaterialEditors {

enum class EditorIcon : u8 {
    Apply, Save, Browse, Home, Palette, LiveUpdate,
    Sphere, Cube, Plane, Cylinder, Mesh,
    Reset, Search,
    Count
};

// File name of an icon inside the icon directory ("Apply.png").
const char* EditorIconFile(EditorIcon icon);

class EditorIconSet {
public:
    // Loads every icon found in `directory`; returns how many loaded.
    u32 Load(Renderer& renderer, const std::filesystem::path& directory);
    TextureHandle Get(EditorIcon icon) const { return m_textures[static_cast<usize>(icon)]; }

private:
    std::array<TextureHandle, static_cast<usize>(EditorIcon::Count)> m_textures{};
};

}  // namespace Luma::MaterialEditors
