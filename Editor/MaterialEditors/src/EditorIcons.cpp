#include "Luma/MaterialEditors/EditorIcons.h"

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
        const Slate::Image image = Slate::LoadImage(renderer, file.string());
        m_textures[i] = image.texture;
        if (image.texture != 0) ++loaded;
    }
    if (loaded < m_textures.size()) {
        LUMA_LOG_WARN("MaterialEditors", "Loaded {}/{} editor icons from {}", loaded, m_textures.size(),
                      directory.string());
    }
    return loaded;
}

}  // namespace Luma::MaterialEditors
