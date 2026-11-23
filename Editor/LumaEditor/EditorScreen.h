#pragma once

#include <filesystem>
#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Luma/Scene/ActorClipboard.h"
#include "Luma/Asset/AssetPath.h"
#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Asset/LumaMesh.h"
#include "Luma/Core/Registration.h"
#include "Luma/Editor/Panels/ImportDialog.h"
#include "Luma/Editor/AssetWindows.h"
#include "Luma/Editor/MaterialSceneBridge.h"
#include "Luma/Editor/PanelRegistry.h"
#include "Luma/Editor/SettingsPageRegistry.h"
#include "Luma/Editor/Panels/BackgroundTasks.h"
#include "Luma/Editor/Panels/Console.h"
#include "Luma/Editor/Panels/ContentBrowser.h"
#include "Luma/Editor/Panels/Inspector.h"
#include "Luma/Editor/Panels/MaterialEditor.h"
#include "Luma/Editor/Panels/ProjectSettings.h"
#include "Luma/Editor/Panels/GraphicsPages.h"
#include "Luma/Graphics/GraphicsSettings.h"
#include "Luma/Editor/Panels/ProjectSettingsStore.h"
#include "Luma/Editor/Panels/Viewport.h"
#include "Luma/Editor/Panels/Preferences.h"
#include "Luma/Editor/Panels/MeshPlacement.h"
#include "Luma/Editor/Panels/WorldOutliner.h"
#include "MaterialPreview.h"
#include "Luma/MaterialEditors/MaterialGraphEditor.h"
#include "Luma/MaterialEditors/MaterialInstanceEditor.h"
#include "PanelContext.h"
#include "Luma/Gizmo/TransformGizmo.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Math/Math.h"
#include "Luma/Project/Project.h"
#include "Luma/RHI/DebugDraw.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Renderer/DeferredSceneView.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/DockSpace.h"
#include "Luma/VFS/FileWatcher.h"

namespace Luma::Import {
class ImportSession;
class SourceWatch;
}
namespace Luma::Audio {
class AudioPreview;
}

namespace Luma {

// The editor shell: menu bar, toolbar, and a dockable layout (World Outliner,
// Viewport, Inspector, Console). Drives an ECS Scene (EnTT) and orchestrates
// the per-panel modules under Editor/panels/. The panels themselves are
// self-contained; this class only wires them together + owns the shared
// state (scene, camera, gizmo, selection).
class EditorScreen {
public:
    explicit EditorScreen(const std::filesystem::path& projectFile);
    ~EditorScreen();

    bool HasProject() const { return m_project.has_value(); }

    // Builds the scene view (camera + grid + entity instances + gizmo overlay).
    SceneView BuildSceneView();
    void TickSimulation(f64 seconds) { m_runner.Tick(seconds); }

    void Draw(Slate::Context& ui, f32 width, f32 height);
    enum class SettingsKind { Project, Preferences };
    bool SettingsOpen(SettingsKind kind) const;
    void CloseSettings(SettingsKind kind);
    void DrawSettings(SettingsKind kind, Slate::Context& ui, f32 width, f32 height);

    // Persists the latest rendered editor frame as the project-browser card
    // preview. The renderer must still be alive when this is called.
    void CaptureProjectPreview();
    void FinalizeProjectPreview();

    // The project's graphics settings (Project Settings > Engine > Display &
    // Frame Rate / Rendering Quality), edited live; main.cpp applies them.
    const Graphics::GraphicsSettings& GraphicsSettings() const { return m_graphics; }
    // Reported on the Display page ("Swapchain is using: ...").
    void SetActivePresentModeQuery(std::function<std::string()> query) { m_activePresentMode = std::move(query); }

    Slate::Rect ViewportRect() const { return m_viewportPanel.Rect(); }
    void SetViewportTexture(TextureHandle texture) {
        m_viewportPanel.SetTexture(texture);
    }

    // Snapshot key the renderer uses for the selected-camera preview.
    static constexpr u64 kCameraPreviewKey = 0xCA3E12A1u;
    // Fills `out` with a clean scene view (no grid/overlay) from the selected
    // camera's transform and projection, at width x height; returns false when
    // no camera is selected. Must be called after BuildDeferredSceneView, which
    // builds the frame's scene source this reuses. `name` <- the camera's name.
    bool BuildCameraPreviewView(Renderer2::DeferredSceneView& out, u32 width, u32 height,
                                std::string* name);
    // Hands the rendered preview texture (0 = none) and camera name to the
    // viewport panel for its bottom-right inset.
    void SetCameraPreview(TextureHandle texture, std::string name) {
        m_viewportPanel.SetCameraPreview(texture, std::move(name));
    }
    void SetToolbarIcons(TextureHandle play, TextureHandle pause,
                         TextureHandle stop) {
        m_iconPlay = play;
        m_iconPause = pause;
        m_iconStop = stop;
    }

    // Unreal icon-pack textures (Content/Editor/Icons/ue) used by the panel
    // tabs, the main toolbar, the Output Log badges and the viewport toolbar.
    struct IconSet {
        TextureHandle tabOutliner = 0, tabDetails = 0, tabViewport = 0;
        TextureHandle tabContent = 0, tabOutputLog = 0;
        TextureHandle save = 0, error = 0, warning = 0;
        Editor::Panels::ViewportIcons viewport;
    };
    // Also registers the tab textures as named editor images ("TabOutliner",
    // "TabDetails", "TabViewport", "TabContent", "TabOutputLog").
    void SetEditorIcons(const IconSet& icons);

    // Renders `materialPath` (virtual path) in the preview scene to a PNG
    // (the --preview-capture flag). False without a preview renderer.
    bool CapturePreview(const std::string& materialPath, const std::filesystem::path& png,
                        u32 size);

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

    // "Import to Current Folder": native multi-select picker, then the import
    // window. Dropped files and folders take the same path.
    void ImportAssetsInto(const std::filesystem::path& folder);
    void SetAudioPreview(Audio::AudioPreview* preview) { m_audioPreview = preview; }
    void OpenImport(const std::vector<std::filesystem::path>& files, const std::filesystem::path& folder,
                    bool reimport);
    void ReimportAsset(const std::filesystem::path& asset);
    // Reading a source file (Assimp, image/audio decode) can take seconds, so
    // it runs on the Job System; the Import window opens when it finishes.
    void BeginTranslate(std::string title, std::filesystem::path folder, bool reimport,
                        std::function<std::unique_ptr<Import::ImportSession>(std::string* err)> work);
    void PollTranslate();
    void FinishOpenImport(std::unique_ptr<Import::ImportSession> session, const std::filesystem::path& folder,
                          bool reimport);
    void PollImport();
    void OnImportDialog(Editor::Panels::ImportDialog::Action action);

    // Wires the renderer: thumbnails, and the material preview scene on the
    // backend's secondary deferred renderer (GPU material/mesh thumbnails).
    void SetRenderer(Luma::Renderer* renderer);
    // Engine services owned by the app (must outlive the editor).
    void SetBackgroundServices(Jobs::JobSystem* jobs, Tasks::TaskManager* tasks);
    Luma::Renderer* Renderer() const noexcept { return m_renderer; }
    
    // Build scene view for deferred renderer. `width`/`height` are the
    // current viewport pixel size and drive the projection aspect ratio so
    // objects keep their proportions when the panel is resized.
    // `deltaSeconds` = frame time; time-based effects (auto-exposure
    // adaptation) use it. 0 makes them snap instead of animating.
    Renderer2::DeferredSceneView BuildDeferredSceneView(u32 width, u32 height,
                                                        f32 deltaSeconds = 0.0f);


    // Forwards the Create-menu header button icon to the outliner panel.
    // A zero handle falls back to a pack plus glyph.
    void SetCreateButtonIcon(TextureHandle icon) {
        m_outlinerPanel.SetCreateButtonIcon(icon);
    }

    // Forwards the Create-menu search-glass texture (from the ThirdParty
    // Unreal icon pack). A zero handle falls back to the pack glyph.
    void SetSearchGlassIcon(TextureHandle icon) {
        m_outlinerPanel.SetSearchGlassIcon(icon);
    }

    // Forwards Content Browser chrome icons to the panel. Missing textures
    // (any zero handle) fall back to pack glyphs.
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
    // Spawns a game object from the EntityTemplates() entry `templateId` (the
    // World Outliner's Place Actors menu) and selects it.
    void CreateActor(const std::string& templateId);
    // Warns once per scene load about components kept for missing plugins.
    void ReportMissingComponentPlugins();
    bool LoadScene();
    void SaveScene();
    // Writes every unsaved material asset; false if any failed.
    bool SaveMaterialAssets();

    // External edits under Assets/: materials reload, textures and meshes
    // re-upload. Watches are refreshed when the asset registry changes.
    void PollAssetChanges(f32 deltaSeconds);
    void RefreshAssetWatches();
    // Re-renders the thumbnails of the asset at `virtualPath` and of every
    // material / instance that depends on it (nothing else), like Unreal's
    // per-package thumbnail invalidation.
    void RefreshThumbnailsFor(const std::string& virtualPath);
    void OnAssetFileChanged(const std::filesystem::path& file);
    void DrawClosePrompt(Slate::Context& ui);

    // Material Editor tabs (one per open asset).
    void OpenMaterialEditor(const std::string& assetPath);
    // Node-graph Material Editor / Material Instance Editor (graph assets).
    // editor: "" = default (the Instance Editor), "instance", "material".
    bool OpenGraphMaterialEditor(const std::string& assetPath, const std::string& editor = {});
    MaterialEditors::MaterialEditorServices MakeMaterialEditorServices();
    void LoadMaterialEditorAssets();
    bool IsGraphEditor(const std::string& assetPath) const;
    bool SaveGraphEditor(const std::string& assetPath, std::string* error);
    void DrawMaterialEditorClosePrompt(Slate::Context& ui);
    TextureHandle AssetThumbnail(const std::string& assetPath);
    // Content Browser material workflows.
    void CreateMaterialAsset(const std::filesystem::path& folder);
    void CreateMaterialInstanceAsset(const std::string& parent, const std::filesystem::path& folder);
    void CreateMaterialFromTextures(const std::filesystem::path& texture);
    void DropMaterial(const Math::Vec3& origin, const Math::Vec3& dir, const std::string& path);
    // Static mesh drag and drop into the level: where it would land (bridge
    // preview), and the real placement on release.
    std::optional<Math::Vec3> MeshDropPosition(const Math::Vec3& origin, const Math::Vec3& dir,
                                               const std::string& mesh);
    void PreviewMeshDrop(const Math::Vec3& origin, const Math::Vec3& dir, const std::string& mesh);
    void DropMesh(const Math::Vec3& origin, const Math::Vec3& dir, const std::string& mesh);
    // `desired` if no material asset uses it, else desired_1, desired_2, ...
    std::string UniqueMaterialPath(const std::string& desired);
    std::vector<std::string> ListAssets(bool materials) const;

    // Entity / asset actions behind the panels' menus (PanelContext hooks).
    void HandleShortcuts(Slate::Context& ui);
    // Nearest actor hit by a world ray (bounds of its mesh / a small sphere
    // for non-mesh actors). kNullEntity when nothing is hit.
    Entity PickEntity(const Math::Vec3& origin, const Math::Vec3& dir) const;
    // Nearest mesh actor surface along the ray (its bounds), in world space.
    std::optional<Editor::Panels::PlacementHit> PickSurface(const Math::Vec3& origin,
                                                            const Math::Vec3& dir) const;
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
    void CutEntities(Entity clicked);
    void CopyEntities(Entity clicked);
    void PasteEntities(Entity clicked, bool asChild);
    void CreateEmptyChild(Entity parent);
    void SelectChildren(Entity parent);
    void MoveToRoot(Entity entity);
    void HideEntities(Entity clicked);
    void ShowAllActors();
    void SelectOnly(Entity entity);
    std::vector<Entity> ActionRoots(Entity clicked) const;
    bool IsHidden(Entity entity) const;
    void CreateSceneAsset(const std::filesystem::path& folder);
    void DeleteAssetPath(const std::filesystem::path& path);
    void RevealInExplorer(const std::filesystem::path& path);
    // Registers the editor's built-in menus, menu entries and dock panels
    // (through the same registries plugins use).
    void RegisterEditorUi();
    // Docks a Material Editor tab for an already-created editor.
    void DockMaterialEditor(Editor::Panels::MaterialEditorPanel* editor);
    // Open/closed panels persisted per project (Saved/Config/EditorLayout.json).
    std::filesystem::path LayoutStatePath() const;
    void LoadPanelLayout();
    void SavePanelLayout();

    // ---- Shared state the panels read/write via PanelContext ----------------
    std::optional<Project> m_project;
    std::string m_title;

    Scene m_authoringScene;
    SceneRunner m_runner;
    SimulationSettings m_simulation;
    Scene& ActiveScene() { return m_runner.PlayScene() ? *m_runner.PlayScene() : m_authoringScene; }
    const Scene& ActiveScene() const { return m_runner.PlayScene() ? *m_runner.PlayScene() : m_authoringScene; }
    void BeginPlay();
    void StopPlay();
    Entity m_authoringSelected = kNullEntity;
    std::vector<Entity> m_authoringSelection;
    ActorClipboard m_authoringClipboard;
    int m_authoringNextNumber = 1;
    Entity m_selected = kNullEntity;
    std::vector<Entity> m_selection;
    ActorClipboard m_clipboard;
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
    // Hosts the registered panels (Editor::EditorPanels()) in m_dock.
    std::unique_ptr<Editor::EditorPanelHost> m_panelHost;
    bool m_layoutBuilt = false;
    // Everything the editor registered (menus, panels, images, settings pages);
    // released first when the editor goes away.
    RegistrationSet m_uiRegistrations;

    // Project Settings: the Maps & Modes page's values, the Config/ store and the
    // window (Edit -> Project Settings).
    Editor::Panels::MapsAndModesSettings m_mapsAndModes;
    Graphics::GraphicsSettings m_graphics;
    std::function<std::string()> m_activePresentMode;
    std::unique_ptr<Editor::Panels::ProjectSettingsStore> m_settingsStore;
    Editor::Panels::ProjectSettingsPanel m_projectSettings{Editor::SettingsPages()};
    // The scene file open in the editor (saved back to the same file).
    std::filesystem::path m_scenePath;

    // Content Browser — wires to the project's Content/ folder so the
    // registry is populated before the panel is drawn each frame.
    AssetRegistry m_assetRegistry;
    
    // Material assets by virtual path (/Game -> Assets/, /Engine -> engine
    // Content/Engine) and the bridge that makes the scene's materials, meshes
    // and textures resident in the renderer.
    AssetPathResolver m_assetPaths;
    std::unique_ptr<Material::MaterialLibrary> m_materials;
    std::unique_ptr<Import::ImportSession> m_importSession;
    struct PendingTranslate;
    std::shared_ptr<PendingTranslate> m_translate;  // source being read for the Import window
    // Cached "source changed?" per imported asset; checked on the Job System.
    std::unique_ptr<Import::SourceWatch> m_sourceWatch;
    Editor::Panels::ImportDialog m_importDialog;
    std::filesystem::path m_importDest;
    std::string m_importVirtual;
    bool m_importActive = false;
    Tasks::TaskToken m_importTask;  // Background Tasks entry while an import runs
    u64 m_importTotal = 0;
    Audio::AudioPreview* m_audioPreview = nullptr;
    Editor::MaterialSceneBridge m_bridge;
    std::vector<LineVertex> m_overlayLines;  // gizmo + DebugDraw overlay, this frame
    std::vector<LineVertex> m_debugLines;    // DebugDraw depth-tested lines, this frame
    VFS::FileWatcher m_assetWatcher;
    std::unordered_set<std::string> m_watchedFiles;
    usize m_watchedRegistrySize = 0;
    f32 m_watchTimer = 0.0f;
    // Preview scene for material/mesh thumbnails and the Material Editor.
    std::unique_ptr<MaterialPreview> m_preview;
    std::vector<std::unique_ptr<Editor::Panels::MaterialEditorPanel>> m_materialEditors;
    std::vector<std::unique_ptr<MaterialEditors::MaterialGraphEditor>> m_graphEditors;
    std::vector<std::unique_ptr<MaterialEditors::MaterialInstanceEditor>> m_instanceEditors;
    MaterialEditors::EditorIconSet m_materialEditorIcons;
    std::unique_ptr<Slate::Font> m_materialWatermarkFont;
    std::vector<std::unique_ptr<Slate::Font>> m_materialGraphFonts;
    std::string m_uiFontPath;  // the UI font file (Content/Editor/Fonts/...), for the editors' extra sizes
    bool m_materialEditorAssetsLoaded = false;
    std::string m_closingEditor;                // dirty tab awaiting Save/Discard/Cancel
    std::string m_closingWindow;                // its asset window id ("" = a dock tab)
    // Asset editor windows (window row above the toolbar; Home = level editor).
    Editor::AssetWindowManager m_windows;
    Slate::Rect m_windowWorkArea{0, 60, 1280, 640};  // under the row, above the status bar
    std::vector<std::string> m_editorsToDelete;  // closed tabs, freed after the dock draws
    bool m_closeRequested = false;
    bool m_closeConfirmed = false;


    TextureHandle m_iconPlay = 0;
    TextureHandle m_iconPause = 0;
    TextureHandle m_iconStop = 0;
    IconSet m_icons;

    // ---- Panels (one per docked pane) ---------------------------------------
    Editor::Panels::WorldOutlinerPanel m_outlinerPanel;
    Editor::Panels::ViewportPanel m_viewportPanel;
    Editor::Panels::PreferencesPanel m_preferences;
    bool m_preferencesOpen = false;
    Editor::Panels::InspectorPanel m_inspectorPanel;
    Editor::Panels::ConsolePanel m_consolePanel;
    Editor::Panels::ContentBrowserPanel m_contentBrowser;
    Editor::Panels::BackgroundTasksPanel m_backgroundTasks;
    Jobs::JobSystem* m_jobs = nullptr;
    Tasks::TaskManager* m_tasks = nullptr;

    // Renderer for thumbnail generation
    Luma::Renderer* m_renderer = nullptr;


    // Holds the source SceneView for the deferred pipeline. Must outlive the
    // DeferredSceneView returned by BuildDeferredSceneView (its sceneData /
    // lightingParams members point into this).
    SceneView m_deferredSceneSource;
    // Same, for the camera-preview view (a grid/overlay-free copy of the frame's
    // scene source). Must outlive the preview DeferredSceneView.
    SceneView m_cameraPreviewSceneSource;

    Editor::Panels::PanelContext BuildPanelContext();
};

}  // namespace Luma
