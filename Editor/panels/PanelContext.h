#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Luma/Math/Math.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Slate/Context.h"

namespace Luma::Material {
class MaterialLibrary;
}
namespace Luma::Jobs {
class JobSystem;
}
namespace Luma::Tasks {
class TaskManager;
}

namespace Luma {
// Forward declarations keep PanelContext.h lightweight; only the panels that
// actually touch the gizmo need to include the full TransformGizmo header.
class TransformGizmo;
class Renderer;
class AssetRegistry;
}  // namespace Luma

namespace Luma::Editor::Panels {

using GizmoPtr = Luma::TransformGizmo*;

struct PanelContext {
    // Renderer for uploading dynamic textures (e.g. asset thumbnails).
    Renderer* renderer = nullptr;

    // Asset index (rooted at the project's Content/ folder). Panels that
    // pick assets read the texture list from this.
    AssetRegistry* assetRegistry = nullptr;

    // Scene + ECS the panels drive.
    Scene* scene = nullptr;
    Entity* selected = nullptr;        // outliner sets, inspector + gizmo read

    // Orbit camera (viewport + inspector preview).
    f32* camYaw = nullptr;
    f32* camPitch = nullptr;
    f32* camDistance = nullptr;
    Math::Vec3* camTarget = nullptr;
    f32 fovY = 0.9f;
    f32 nearZ = 0.1f;
    f32 farZ = 500.0f;
    Math::Mat4* view = nullptr;        // rebuilt each BuildSceneView
    f32* gizmoScale = nullptr;

    // Gizmo (viewport hosts it; selection feeds it).
    GizmoPtr gizmo = nullptr;

    // EditorScreen hooks (lightweight callbacks so panels don't have to
    // know about the EditorScreen type).
    // Outliner's Place Actors menu: instantiate the EntityTemplates() entry `id`.
    std::function<void(const std::string& templateId)> onCreateEntity;
    std::function<void()> onSaveScene;     // File menu "Save Scene"

    // Content Browser right-click menu "GET > Import to Current Folder":
    // opens a native file picker and imports the chosen files into `folder`
    // (empty = content root), then rescans the registry. Implemented by
    // EditorScreen; panels just invoke it.
    std::function<void(const std::filesystem::path& folder)> onImportAssets;

    // Content Browser: create an empty scene asset in `folder`; move a file
    // or folder to the platform recycle bin (recoverable); reveal a path in
    // the system file browser.
    std::function<void(const std::filesystem::path& folder)> onCreateScene;
    std::function<void(const std::filesystem::path& path)> onDeleteAsset;
    std::function<void(const std::filesystem::path& path)> onRevealInExplorer;

    // World Outliner entity actions (EditorScreen owns naming, selection
    // and camera state): duplicate (selects the copy), delete, and frame
    // the entity in the viewport.
    std::function<void(Entity)> onDuplicateEntity;
    std::function<void(Entity)> onDeleteEntity;
    std::function<void(Entity)> onFocusEntity;
    // Viewport camera: back to the default orbit around the origin.
    std::function<void()> onResetCamera;
    // Viewport click-select: a world-space ray through the clicked pixel.
    // EditorScreen selects the nearest actor it hits (or clears selection).
    std::function<void(const Math::Vec3& origin, const Math::Vec3& dir)> onPickRay;

    // ---- Materials ----------------------------------------------------------
    // The editor's material assets (edits are live in every viewport).
    Material::MaterialLibrary* materials = nullptr;
    // Engine services: background work (Job System) and its user-visible
    // progress (Task Manager). Owned by the app; may be null in tests.
    Jobs::JobSystem* jobs = nullptr;
    Tasks::TaskManager* tasks = nullptr;
    // Number of material sections of an actor's mesh (1 for primitives).
    std::function<u32(Entity)> sectionCount;
    // Renders a material on a preview shape (0 sphere, 1 cube, 2 plane,
    // 3 cylinder) orbiting by yaw/pitch at width x height; `sky` shows the sky
    // backdrop (else a dark studio). Returns the UI texture (0 = unavailable).
    std::function<TextureHandle(const std::string& materialPath, int shape, f32 yaw,
                                f32 pitch, u32 width, u32 height, bool sky)>
        renderMaterialPreview;
    // Thumbnail texture for an asset virtual path (0 while unavailable).
    std::function<TextureHandle(const std::string& assetPath)> assetThumbnail;
    // Virtual paths of every texture / material (+ instance) asset.
    std::function<std::vector<std::string>()> listTextures;
    std::function<std::vector<std::string>()> listMaterials;
    // Opens an asset's editor (e.g. a Material Editor tab).
    std::function<void(const std::string& assetPath)> onOpenAsset;
    // Shows an asset in the Content Browser.
    std::function<void(const std::string& assetPath)> onBrowseToAsset;
    // Virtual path of the Content Browser's selected asset ("" when none).
    std::function<std::string()> selectedAsset;
    // Virtual asset path of a file under the project/engine content ("" if
    // outside both).
    std::function<std::string(const std::filesystem::path& file)> virtualPathOf;
    // Content Browser material workflows (each saves the asset and opens it).
    std::function<void(const std::filesystem::path& folder)> onCreateMaterial;
    std::function<void(const std::string& parent, const std::filesystem::path& folder)>
        onCreateMaterialInstance;
    // Builds a material from the texture set `texture` belongs to.
    std::function<void(const std::filesystem::path& texture)> onCreateMaterialFromTextures;
    // A material dropped on the viewport along a world ray.
    std::function<void(const Math::Vec3& origin, const Math::Vec3& dir,
                       const std::string& materialPath)>
        onDropMaterial;
};

}  // namespace Luma::Editor::Panels
