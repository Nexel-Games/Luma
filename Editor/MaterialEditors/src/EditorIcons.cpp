#include "Luma/MaterialEditors/EditorIcons.h"

#include <algorithm>
#include <vector>

#include "Luma/Core/Log.h"
#include "Luma/Slate/Image.h"

namespace Luma::MaterialEditors {

const char* EditorIconFile(EditorIcon icon) {
    switch (icon) {
        case EditorIcon::Apply: return "Apply.png";
        case EditorIcon::Save: return "Save.png";
        case EditorIcon::Browse: return "Browse.png";
        case EditorIcon::Home: return "Home.png";
        case EditorIcon::Palette: return "Palette.png";
        case EditorIcon::LiveUpdate: return "LiveUpdate.png";
        case EditorIcon::Sphere: return "Sphere.png";
        case EditorIcon::Cube: return "Cube.png";
        case EditorIcon::Plane: return "Plane.png";
        case EditorIcon::Cylinder: return "Cylinder.png";
        case EditorIcon::Mesh: return "Mesh.png";
        case EditorIcon::Reset: return "Reset.png";
        case EditorIcon::Search: return "Search.png";
        case EditorIcon::Count: break;
    }
    return "";
}

u32 EditorIconSet::Load(Renderer& renderer, const std::filesystem::path& directory) {
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
        for (usize p = 0; p + 3 < pixels.size(); p += 4) {
            const f32 lum = (0.2126f * pixels[p] + 0.7152f * pixels[p + 1] + 0.0722f * pixels[p + 2]) / 255.0f;
            const f32 keep = std::clamp((lum - 0.12f) / 0.45f, 0.0f, 1.0f);
            pixels[p + 3] = static_cast<u8>(static_cast<f32>(pixels[p + 3]) * keep + 0.5f);
            pixels[p] = pixels[p + 1] = pixels[p + 2] = 255;
        }
        m_textures[i] = renderer.CreateTexture(w, h, pixels.data());
        if (m_textures[i] != 0) ++loaded;
    }
    if (loaded < m_textures.size()) {
        LUMA_LOG_WARN("MaterialEditors", "Loaded {}/{} editor icons from {}", loaded, m_textures.size(),
                      directory.string());
    }
    return loaded;
}

}  // namespace Luma::MaterialEditors
