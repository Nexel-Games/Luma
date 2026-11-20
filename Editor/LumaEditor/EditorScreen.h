#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Asset/AssetImportManager.h"
#include "Luma/Asset/AssetFileWatcher.h"
#include "Luma/Asset/LumaMesh.h"
#include "Luma/Editor/MaterialSceneBridge.h"
#include "Luma/Editor/Panels/Console.h"
#include "Luma/Editor/Panels/ContentBrowser.h"
#include "Luma/Editor/Panels/Inspector.h"
#include "Luma/Editor/Panels/Viewport.h"
#include "Luma/Editor/Panels/WorldOutliner.h"
#include "PanelContext.h"
#include "Luma/Gizmo/TransformGizmo.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Math/Math.h"
#include "Luma/Project/Project.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Renderer/DeferredSceneView.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/DockSpace.h"
#include "Luma/VFS/FileWatcher.h"

namespace Luma {

// The editor shell: menu bar, toolbar, and a dockable layout (World Outliner,
// Viewport, Inspector, Console). Drives an ECS Scene (EnTT) and orchestrates
// the per-panel modules under Editor/panels/. The panels themselves are
// self-contained; this class only wires them together + owns the shared
// state (scene, camera, gizmo, selection).
class EditorScreen {
public:
    explicit EditorScreen(const std::filesystem::path& projectFile);

    bool HasProject() const { return m_project.has_value(); }

    // Builds the scene view (camera + grid + entity instances + gizmo overlay).
    SceneView BuildSceneView();

    void Draw(Slate::Context& ui, f32 width, f32 height);

    // Persists the latest rendered editor frame as the project-browser card
    // preview. The renderer must still be alive when this is called.
    void CaptureProjectPreview();
    void FinalizeProjectPreview();

    Slate::Rect ViewportRect() const { return m_viewportPanel.Rect(); }
    void SetViewportTexture(TextureHandle texture) {
        m_viewportPanel.SetTexture(texture);
    }
    void SetToolbarIcons(TextureHandle play, TextureHandle pause,
                         TextureHandle stop) {
        m_iconPlay = play;
        m_iconPause = pause;
        m_iconStop = stop;
    }
    void SetLogoIcon(TextureHandle logo) { m_iconLogo = logo; }

    // Unreal icon-pack textures (Content/Editor/Icons/ue) used by the panel
    // tabs, the main toolbar, the Output Log badges and the viewport toolbar.
    struct IconSet {
        TextureHandle tabOutliner = 0, tabDetails = 0, tabViewport = 0;
        TextureHandle tabContent = 0, tabOutputLog = 0;
        TextureHandle save = 0, error = 0, warning = 0;
        Editor::Panels::ViewportIcons viewport;
    };
    void SetEditorIcons(const IconSet& icons) {
        m_icons = icons;
        m_viewportPanel.SetIcons(icons.viewport);
        m_consolePanel.SetBadgeIcons(icons.error, icons.warning);
    }

    // Selects the first entity named `name` (the --select capture flag).
    // Returns false when no entity has that name.
    bool SelectEntityByName(const std::string& name);

    // Material assets with edits not written to disk yet (e.g. instances
    // created by migrating an old scene). Saving the scene saves them too.
    std::vector<std::string> UnsavedAssets() const;
    // The window asked to close: confirmed at once when nothing is unsaved,
    // otherwise the Save All / Don't Save / Cancel prompt opens.
    void RequestClose();
    bool CloseConfirmed() const { return m_closeConfirmed; }

    // Handle dropped files from the OS (forwarded to Content Browser)
    void HandleDroppedFiles(const std::vector<std::filesystem::path>& files);

    // "Import to Current Folder" (Content Browser right-click menu): opens a
    // native multi-select file dialog, copies the picked files into `folder`
    // (empty = content root) and rescans the asset registry.
    void ImportAssetsInto(const std::filesystem::path& folder);

    // Wire the renderer for thumbnail generation
    void SetRenderer(Luma::Renderer* renderer) {
        m_renderer = renderer;
        m_contentBrowser.SetRenderer(renderer);
    }
    Luma::Renderer* Renderer() const noexcept { return m_renderer; }
    
    // Build scene view for deferred renderer. `width`/`height` are the
    // current viewport pixel size and drive the projection aspect ratio so
    // objects keep their proportions when the panel is resized.
    // `deltaSeconds` = frame time; time-based effects (auto-exposure
    // adaptation) use it. 0 makes them snap instead of animating.
    Renderer2::DeferredSceneView BuildDeferredSceneView(u32 width, u32 height,
                                                        f32 deltaSeconds = 0.0f);


    // Forwards the Create-menu header button icon to the outliner panel.
    // A zero handle falls back to a procedural plus glyph.
    void SetCreateButtonIcon(TextureHandle icon) {
        m_outlinerPanel.SetCreateButtonIcon(icon);
    }

    // Forwards the Create-menu search-glass texture (from the ThirdParty
    // Unreal icon pack). A zero handle falls back to the procedural glyph.
    void SetSearchGlassIcon(TextureHandle icon) {
        m_outlinerPanel.SetSearchGlassIcon(icon);
    }

    // Forwards the Create-menu category icons (Geometry / Lights /
    // Environment — Unreal-style silhouettes, drawn gray in the menu).
    // Zero handles fall back to the procedural glyphs.
    void SetCategoryIcons(TextureHandle geometry, TextureHandle light,
                          TextureHandle environment) {
        m_outlinerPanel.SetCategoryIcons(geometry, light, environment);
    }

    // Forwards the primitive-actor icons for the Geometry submenu rows
    // (Unreal primitive thumbnails, drawn gray). Zero handles fall back to
    // the procedural glyphs.
    void SetPrimitiveIcons(TextureHandle cube, TextureHandle plane,
                           TextureHandle sphere, TextureHandle cylinder) {
        m_outlinerPanel.SetPrimitiveIcons(cube, plane, sphere, cylinder);
    }

    // Forwards the outliner actor icons (light types + generic mesh) shown
    // beside entity names in the World Outliner list. Zero handles fall back
    // to no icon.
    void SetOutlinerActorIcons(TextureHandle dirLight, TextureHandle pointLight,
                               TextureHandle spotLight, TextureHandle mesh) {
        m_outlinerPanel.SetOutlinerActorIcons(dirLight, pointLight, spotLight,
                                              mesh);
    }

    // Forwards Content Browser chrome icons to the panel. Missing textures
    // (any zero handle) fall back to procedural glyphs.
    void SetContentBrowserIcons(TextureHandle sortUp, TextureHandle sortDown,
                                TextureHandle searchGlass,
                                TextureHandle folder, TextureHandle reload,
                                TextureHandle importTex,
                                TextureHandle openFolder,
                                TextureHandle expandArrow,
                                TextureHandle retractArrow) {
        m_contentBrowser.SetIcons(sortUp, sortDown, searchGlass, folder,
                                   reload, importTex, openFolder, expandArrow,
                                   retractArrow);
    }

private:
    // Spawns a new game object from the World Outliner's Create menu
    // (Empty / primitives / Light > directional, point, spot, tube / Environment).
    void CreateActor(Editor::Panels::CreateActorKind kind);
    void CreateEnvironment();
    bool LoadScene();
    void SaveScene();
    // Writes every unsaved material asset; false if any failed.
    bool SaveMaterialAssets();

    // External edits under Assets/: materials reload, textures and meshes
    // re-upload. Watches are refreshed when the asset registry changes.
    void PollAssetChanges(f32 deltaSeconds);
    void RefreshAssetWatches();
    void OnAssetFileChanged(const std::filesystem::path& file);
    void DrawClosePrompt(Slate::Context& ui);

    // Entity / asset actions behind the panels' menus (PanelContext hooks).
    void HandleShortcuts(Slate::Context& ui);
    // Nearest actor hit by a world ray (bounds of its mesh / a small sphere
    // for non-mesh actors). kNullEntity when nothing is hit.
    Entity PickEntity(const Math::Vec3& origin, const Math::Vec3& dir) const;
    // `path` relative to the project folder (for user-facing messages).
    std::string ProjectRelative(const std::filesystem::path& path) const;

public:
    // Log history shown by the Output Log (Console) panel.
    void SetLogHistory(std::shared_ptr<LogBuffer> history) {
        m_consolePanel.SetSource(std::move(history));
    }

private:
    void DuplicateEntity(Entity e);
    void DeleteEntity(Entity e);
    void FocusEntity(Entity e);
    void CreateSceneAsset(const std::filesystem::path& folder);
    void DeleteAssetPath(const std::filesystem::path& path);
    void RevealInExplorer(const std::filesystem::path& path);
    // Main menu bar menus (File / Edit / Assets / Window).
    std::vector<Slate::MenuEntry> BuildMainMenu(int index);
    void BuildDock();

    // ---- Shared state the panels read/write via PanelContext ----------------
    std::optional<Project> m_project;
    std::string m_title;

    Scene m_scene;
    Entity m_selected = kNullEntity;
    Entity m_environment = kNullEntity;
    int m_nextNumber = 1;

    TransformGizmo m_gizmo;
    std::vector<SceneInstance> m_instances;
    std::vector<SceneLight> m_lights;

    static constexpr f32 kDefaultCamYaw = 0.9f;
    static constexpr f32 kDefaultCamPitch = 0.5f;
    static constexpr f32 kDefaultCamDistance = 12.0f;
    f32 m_camYaw = kDefaultCamYaw;
    f32 m_camPitch = kDefaultCamPitch;
    f32 m_camDistance = kDefaultCamDistance;
    Math::Vec3 m_camTarget{0.0f, 0.0f, 0.0f};

    Math::Mat4 m_view = Math::Mat4::Identity();
    f32 m_fovY = 0.9f;
    f32 m_nearZ = 0.1f;
    f32 m_farZ = 500.0f;
    f32 m_gizmoScale = 1.0f;

    Slate::DockSpace m_dock;
    bool m_dockBuilt = false;

    // Content Browser — wires to the project's Content/ folder so the
    // registry is populated before the panel is drawn each frame.
    AssetRegistry m_assetRegistry;
    
    // Material assets by virtual path (/Game -> Assets/, /Engine -> engine
    // Content/Engine) and the bridge that makes the scene's materials, meshes
    // and textures resident in the renderer.
    AssetPathResolver m_assetPaths;
    std::unique_ptr<Material::MaterialLibrary> m_materials;
    Editor::MaterialSceneBridge m_bridge;
    VFS::FileWatcher m_assetWatcher;
    std::unordered_set<std::string> m_watchedFiles;
    usize m_watchedRegistrySize = 0;
    f32 m_watchTimer = 0.0f;
    bool m_closeRequested = false;
    bool m_closeConfirmed = false;


    TextureHandle m_iconPlay = 0;
    TextureHandle m_iconPause = 0;
    TextureHandle m_iconStop = 0;
    TextureHandle m_iconLogo = 0;
    IconSet m_icons;

    // ---- Panels (one per docked pane) ---------------------------------------
    Editor::Panels::WorldOutlinerPanel m_outlinerPanel;
    Editor::Panels::ViewportPanel m_viewportPanel;
    Editor::Panels::InspectorPanel m_inspectorPanel;
    Editor::Panels::ConsolePanel m_consolePanel;
    Editor::Panels::ContentBrowserPanel m_contentBrowser;

    // Renderer for thumbnail generation
    Luma::Renderer* m_renderer = nullptr;


    // Holds the source SceneView for the deferred pipeline. Must outlive the
    // DeferredSceneView returned by BuildDeferredSceneView (its sceneData /
    // lightingParams members point into this).
    SceneView m_deferredSceneSource;

    Editor::Panels::PanelContext BuildPanelContext();
};

}  // namespace Luma
