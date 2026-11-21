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
namespace Luma::Audio {
class AudioPreview;
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

// What the outliner's "Create GameObject" menu can spawn.
enum class CreateActorKind {
    Empty,
    Cube,
    Plane,
    Sphere,
    Cylinder,
    DirectionalLight,
    PointLight,
    SpotLight,
    TubeLight,
    Environment,
    PostProcessVolume,
};

// One material slot of a static mesh, as the Inspector shows it.
struct MeshSlotInfo {
    std::string name;             // "Paint", "Element 0"
    std::string defaultMaterial;  // the mesh's own material (virtual path) or ""
};

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
    std::function<void(CreateActorKind)> onCreateActor;  // Outliner's create menu
    std::function<void()> onSaveScene;     // File menu "Save Scene"

    // Content Browser right-click menu "GET > Import to Current Folder":
    // opens a native file picker and imports the chosen files into `folder`
    // (empty = content root), then rescans the registry. Implemented by
    // EditorScreen; panels just invoke it.
    std::function<void(const std::filesystem::path& folder)> onImportAssets;
    // Reimport an asset that has a .meta (Content Browser context menu).
    std::function<void(const std::filesystem::path& asset)> onReimport;
    // True when the asset's recorded source no longer matches its .meta.
    std::function<bool(const std::filesystem::path& asset)> sourceChanged;
    // One-shot sound preview. Null when no device is available.
    Audio::AudioPreview* audioPreview = nullptr;

    // Content Browser: create an empty scene asset in `folder`; move a file
    // or folder to the platform recycle bin (recoverable); reveal a path in
    // the system file browser.
    std::function<void(const std::filesystem::path& folder)> onCreateScene;
    std::function<void(const std::filesystem::path& path)> onDeleteAsset;
    // An asset or folder was renamed in the Content Browser (virtual paths,
    // extension-less; folders too): references to it should follow.
    std::function<void(const std::string& oldPath, const std::string& newPath)> onAssetRenamed;
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
    // Material slots of an actor's static mesh (name + the mesh's own
    // material per section); empty for primitives or a mesh not loaded yet.
    std::function<std::vector<MeshSlotInfo>(Entity)> meshSections;
    // Virtual paths of every static mesh (.lmesh) asset.
    std::function<std::vector<std::string>()> listMeshes;
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
    // A static mesh dragged over the viewport (every frame, along the world ray
    // under the cursor), dropped there, or the drag left/ended/was cancelled.
    std::function<void(const Math::Vec3& origin, const Math::Vec3& dir, const std::string& mesh)>
        onMeshDragPreview;
    std::function<void(const Math::Vec3& origin, const Math::Vec3& dir, const std::string& mesh)>
        onDropMesh;
    std::function<void()> onMeshDragEnd;
    // A material dropped on the viewport along a world ray.
    std::function<void(const Math::Vec3& origin, const Math::Vec3& dir,
                       const std::string& materialPath)>
        onDropMaterial;
};

}  // namespace Luma::Editor::Panels
