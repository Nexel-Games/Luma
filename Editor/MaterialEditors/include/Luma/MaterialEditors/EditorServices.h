#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/RHI/Renderer.h"

// What the material editors need from the host editor, as callbacks, so the
// module does not depend on the editor executable, the renderer backend or
// the Content Browser. Every member may be empty (the editors degrade: no
// preview image, empty pickers, no compile status).

namespace Luma::MaterialEditors {

enum class PreviewShape : u8 { Sphere, Cube, Plane, Cylinder, Mesh };

struct PreviewRequest {
    std::string materialPath;   // material or instance (virtual path)
    PreviewShape shape = PreviewShape::Sphere;
    std::string meshPath;       // PreviewShape::Mesh
    f32 yaw = 0.6f, pitch = 0.3f;  // orbit (radians)
    u32 width = 0, height = 0;
    bool floor = true;          // a floor under the mesh (lit preview stage)
    bool sky = true;            // sky backdrop + sky light, else dark studio
};

struct MaterialEditorServices {
    // Renders the preview; returns the UI texture (0 = nothing yet).
    // The editor's adapter wraps Editor/LumaEditor/MaterialPreview.
    std::function<TextureHandle(const PreviewRequest&)> renderPreview;
    // Asset lists (virtual paths) for pickers.
    std::function<std::vector<std::string>()> listMaterials;  // materials + instances
    std::function<std::vector<std::string>()> listMeshes;
    std::function<std::vector<std::string>()> listTextures;
    std::function<void(const std::string&)> browseToAsset;
    // Compile status (MaterialSceneBridge::Errors / PendingCompiles).
    std::function<std::string(std::string_view)> compileErrors;
    std::function<u32()> pendingCompiles;
};

// "/Game/Materials/MI_Red" -> "MI_Red".
std::string AssetName(std::string_view path);

}  // namespace Luma::MaterialEditors
