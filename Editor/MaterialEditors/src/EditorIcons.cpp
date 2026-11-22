#include "Luma/MaterialEditors/EditorIcons.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include "Luma/Core/Log.h"
#include "Luma/Slate/Icons.h"
#include "Luma/Slate/Image.h"

namespace Luma::MaterialEditors {

const char* EditorIconFile(EditorIcon icon) {
    switch (icon) {
        // Luma icon pack (full colour) where it has the icon; the material
        // editor's own themed glyphs otherwise.
        case EditorIcon::Apply: return "Luma_Icons/PNG/32x32/10_Debugging_Engine_Systems/145_success_check.png";
        case EditorIcon::Save: return "Luma_Icons/PNG/32x32/01_Main_Editor_Toolbar/004_save.png";
        case EditorIcon::Browse: return "Luma_Icons/PNG/32x32/04_Content_Browser_Assets/061_folder.png";
        case EditorIcon::Home: return "MaterialEditor/Icons/Home.png";
        case EditorIcon::Palette: return "Luma_Icons/PNG/32x32/05_Rendering_Materials/085_shader_graph.png";
        case EditorIcon::LiveUpdate: return "MaterialEditor/Icons/LiveUpdate.png";
        case EditorIcon::Sphere: return "Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/043_sphere.png";
        case EditorIcon::Cube: return "Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/042_cube.png";
        case EditorIcon::Plane: return "Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/046_plane.png";
        case EditorIcon::Cylinder: return "Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/045_cylinder.png";
        case EditorIcon::Mesh: return "Luma_Icons/PNG/32x32/03_Scene_Objects_Actors/047_static_mesh.png";
        case EditorIcon::Reset: return "Luma_Icons/PNG/32x32/01_Main_Editor_Toolbar/010_undo.png";
        case EditorIcon::Search: return "Luma_Icons/PNG/32x32/01_Main_Editor_Toolbar/016_search.png";
        case EditorIcon::Details: return "Luma_Icons/PNG/32x32/01_Main_Editor_Toolbar/015_settings.png";
        case EditorIcon::Stats: return "Luma_Icons/PNG/32x32/10_Debugging_Engine_Systems/146_profiler.png";
        case EditorIcon::Count: break;
    }
    return "";
}

bool EditorIconKeepsColor(EditorIcon icon) {
    // Luma icon pack artwork is multi-colour metallic: never re-themed.
    return std::string_view(EditorIconFile(icon)).starts_with("Luma_Icons/");
}

u32 EditorIconSet::Load(Renderer& renderer, const std::filesystem::path& directory, Slate::Color top,
                        Slate::Color bottom) {
    m_theme = top;
    u32 loaded = 0;
    for (usize i = 0; i < m_textures.size(); ++i) {
        const std::filesystem::path file = directory / EditorIconFile(static_cast<EditorIcon>(i));
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) continue;
        // White silhouettes (RGB forced to white, alpha kept) so the UI can
        // tint them: text colour at rest, accent when active.
        u32 w = 0, h = 0;
        std::vector<u8> pixels;
        if (!Slate::LoadImagePixels(file.string(), w, h, pixels) || w == 0 || h == 0) continue;
        // Luminance carves the detail: bright strokes stay white, dark
        // outlines / shading become transparent.
        const bool themed = !EditorIconKeepsColor(static_cast<EditorIcon>(i));
        // Visible rows of the glyph, so the gradient spans the icon, not its padding.
        u32 y0 = h, y1 = 0;
        for (u32 y = 0; themed && y < h; ++y)
            for (u32 x = 0; x < w; ++x)
                if (pixels[(static_cast<usize>(y) * w + x) * 4 + 3] > 8) {
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                }
        const f32 span = y1 > y0 ? static_cast<f32>(y1 - y0) : 1.0f;
        for (usize p = 0; themed && p + 3 < pixels.size(); p += 4) {
            const u32 y = static_cast<u32>((p / 4) / w);
            const f32 g = std::clamp((static_cast<f32>(y) - static_cast<f32>(y0)) / span, 0.0f, 1.0f);
            const Slate::Color theme{static_cast<u8>(top.r + (bottom.r - top.r) * g),
                                     static_cast<u8>(top.g + (bottom.g - top.g) * g),
                                     static_cast<u8>(top.b + (bottom.b - top.b) * g), 255};
            const f32 lum = (0.2126f * pixels[p] + 0.7152f * pixels[p + 1] + 0.0722f * pixels[p + 2]) / 255.0f;
            const f32 keep = std::clamp((lum - 0.12f) / 0.45f, 0.0f, 1.0f);
            pixels[p + 3] = static_cast<u8>(static_cast<f32>(pixels[p + 3]) * keep + 0.5f);
            pixels[p] = theme.r;
            pixels[p + 1] = theme.g;
            pixels[p + 2] = theme.b;
        }
        m_textures[i] = renderer.CreateTexture(w, h, pixels.data());
        if (!themed) Slate::MarkFullColorTexture(m_textures[i]);
        m_aspects[i] = static_cast<f32>(w) / static_cast<f32>(h);
        if (m_textures[i] != 0) ++loaded;
    }
    if (loaded < m_textures.size()) {
        LUMA_LOG_WARN("MaterialEditors", "Loaded {}/{} editor icons from {}", loaded, m_textures.size(),
                      directory.string());
    }
    return loaded;
}

}  // namespace Luma::MaterialEditors
