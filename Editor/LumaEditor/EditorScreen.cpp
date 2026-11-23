#include "Luma/Asset/AssetTypeRegistry.h"
#include "EditorScreen.h"
#include "Luma/Profiling/Profiler.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

#include "Luma/AssetImport/ImportSession.h"
#include "Luma/AssetImport/TranslatorRegistry.h"
#include "Luma/AssetImport/SourceWatch.h"
#include "Luma/Jobs/JobSystem.h"
#include "Luma/Tasks/TaskManager.h"
#include "Luma/Asset/LumaMesh.h"
#include "Luma/Asset/LumaMeshFormat.h"
#include "Luma/Asset/ThumbnailRenderer.h"
#include "Luma/Mesh/MeshConversion.h"
#include "Luma/RHI/MaterialGpuDesc.h"
#include "Luma/RHI/SceneRenderer.h"
#include "Luma/Audio/AudioPreview.h"
#include "Luma/Core/Log.h"
#include "Luma/Editor/EditorIcons.h"
#include "Luma/Editor/MenuRegistry.h"
#include "Luma/Editor/PanelRegistry.h"
#include "Luma/Editor/SettingsPageRegistry.h"
#include "Luma/Serialization/Json.h"
#include "Luma/Scene/AssetReferences.h"
#include "Luma/Scene/Components.h"
#include "Luma/Scene/Hierarchy.h"
#include "Luma/Editor/Panels/MaterialSlots.h"
#include "Luma/Editor/Panels/StaticMeshSlot.h"
#include "Luma/Material/MaterialFromTextures.h"
#include "Luma/Scene/MaterialMigration.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/EntityTemplates.h"
#include "Luma/VFS/VFS.h"
#include "Luma/Renderer/DeferredSceneView.h"
#include "Luma/Rendering/Vulkan/VulkanDeferredRenderer.h"
#include "Luma/Slate/Image.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4505)
#endif
#include "stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace Luma {
using Slate::Align;
using Slate::Color;
using Slate::Rect;
using Slate::Vec2;

namespace {
// Engine content (/Engine/...): the mounted Content root's Engine folder,
// else the copy staged next to the executable.
std::filesystem::path EngineContentRoot() {
    auto& vfs = VFS::VFS::Global();
    if (vfs.IsMounted(VFS::Root::Content)) {
        if (auto real = vfs.TryResolve(VFS::Path(VFS::Root::Content, "Engine"))) {
            std::error_code ec;
            if (std::filesystem::is_directory(*real, ec)) return *real;
        }
    }
    return std::filesystem::current_path() / "Content" / "Engine";
}

// Files whose external edits the editor reacts to.
bool IsWatchedAssetFile(const std::filesystem::path& p) {
    static const char* kExtensions[] = {".lmat", ".lmatinst", ".lmesh", ".png", ".jpg",
                                        ".jpeg", ".tga", ".bmp", ".psd", ".gif"};
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    for (const char* e : kExtensions) {
        if (ext == e) return true;
    }
    return false;
}

}  // namespace

EditorScreen::EditorScreen(const std::filesystem::path& projectFile)
    : m_preferences(m_viewportPanel.Settings(), m_gizmo.Snap(), Editor::Panels::PreferencesPanel::UserFile()) {
    m_panelHost = std::make_unique<Editor::EditorPanelHost>(
        Editor::EditorPanels(), m_dock, [this] { return BuildPanelContext(); });
    m_panelHost->onStateChanged = [this] { SavePanelLayout(); };
    RegisterEditorUi();
    std::string err;
    m_project = Project::Load(projectFile, &err);
    if (m_project) {
        m_title = m_project->Name() + " - Luma Editor";
        LUMA_USER_INFO("Project", "Opened project '{}'", m_project->Name());
    } else {
        m_title = "Luma Editor";
        LUMA_USER_ERROR("Project", "Could not open project: {}", err);
    }
    if (m_project) m_assetPaths.SetGameRoot(m_project->AssetsDir());
    m_assetPaths.SetEngineRoot(EngineContentRoot());
    m_materials = std::make_unique<Material::MaterialLibrary>(m_assetPaths);

    LoadPanelLayout();
    if (m_project) {
        // Project Settings pages read their Config/ sections before anything
        // uses them (e.g. Maps & Modes picks the scene to open).
        m_settingsStore = std::make_unique<Editor::Panels::ProjectSettingsStore>(
            Editor::SettingsPages(), m_project->ConfigDir());
        m_settingsStore->LoadAll();
        m_projectSettings.SetStore(m_settingsStore.get());
        m_scenePath = Editor::Panels::EditorStartupScenePath(*m_project, m_mapsAndModes);
    }
    if (!LoadScene()) {
        EnsureAutoCreatedEntities(ActiveScene());
        // New projects already declare a startup scene path. Materialize the
        // default environment scene on first open so the editor and runtime
        // operate on the same persisted scene instead of an in-memory fallback.
        SaveScene();
    }

    // Wire the content browser to an asset registry rooted at the project's
    // Assets/ folder (source assets live directly at the project root, not in
    // a Content/ subdirectory). The panel reads from this; the registry is
    // owned here so we can call Scan() on file-watcher events.
    if (m_project) {
        auto contentRoot = m_project->AssetsDir();
        std::error_code ec;
        if (std::filesystem::exists(contentRoot, ec)) {
            m_assetRegistry.AddRoot(contentRoot);
            m_assetRegistry.Scan();
            LUMA_LOG_INFO("Editor", "content root: {} ({} entries)",
                          contentRoot.string(), m_assetRegistry.Size());
        }
        m_contentBrowser.SetRegistry(&m_assetRegistry);

        // Route the thumbnail cache to <project>/Intermediate/Thumbnails so
        // thumbnails live in the project's scratch area, not inside Assets/.
        ThumbnailManager::Get().SetProjectRoot(m_project->RootDir());
        ThumbnailManager::Get().SetRegistry(&m_assetRegistry);
        RefreshAssetWatches();
    }
}

EditorScreen::~EditorScreen() {
    StopPlay();
    m_preferences.SaveChanges();
    // Unsaved Project Settings edits are written on the way out.
    if (m_projectSettings.IsOpen()) m_projectSettings.Close();
    if (m_settingsStore) m_settingsStore->SaveAll();
    // Unregister before the members the registrations point at go away.
    m_uiRegistrations.Reset();
    // The thumbnail manager outlives the editor; drop the callback into it.
    ThumbnailManager::Get().SetRenderCallback({});
}

void EditorScreen::SetRenderer(Luma::Renderer* renderer) {
    m_renderer = renderer;
    m_contentBrowser.SetRenderer(renderer);
    auto* previewRenderer = renderer ? static_cast<Rendering::VulkanDeferredRenderer*>(
                                           renderer->GetVulkanMaterialDeferredRenderer())
                                     : nullptr;
    if (!previewRenderer) {
        m_preview.reset();
        ThumbnailManager::Get().SetRenderCallback({});
        return;
    }
    m_preview = std::make_unique<MaterialPreview>(*previewRenderer, *m_materials);
    // Material and mesh thumbnails render on the GPU through the preview
    // scene, so they match the viewport (square captures).
    ThumbnailManager::Get().SetRenderCallback(
        [this](AssetType type, const std::filesystem::path& path, u32 width, u32 height,
               std::vector<u8>& rgba) {
            if (!m_preview || width != height) return false;
            LUMA_PROFILE_SCOPE("Thumbnail GPU Render");
            if (type == AssetType::Material) {
                const auto virtualPath = m_assetPaths.ToVirtual(path);
                return virtualPath && m_preview->CaptureMaterial(*virtualPath, width, rgba);
            }
            if (type == AssetType::Mesh) {
                const auto virtualPath = m_assetPaths.ToVirtual(path);
                return virtualPath && m_preview->CaptureMesh(*virtualPath, width, rgba);
            }
            return false;
        });
}

bool EditorScreen::LoadScene() {
    if (!m_project) return false;
    const std::filesystem::path path = m_scenePath;
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return false;
    std::string err;
    LegacyMaterialList legacy;
    if (!SceneSerializer::LoadFromFile(ActiveScene(), path, &err, &legacy)) {
        LUMA_USER_ERROR("Scene", "Failed to load scene '{}': {}", ProjectRelative(path),
                       err);
        return false;
    }
    m_selected = kNullEntity;
    m_selection.clear();
    ShowAllActors();
    m_outlinerPanel.ClearSelection();
    m_inspectorPanel.ClosePopups();
    // Scene setup: every auto-created template (e.g. the sky) the scene lacks.
    EnsureAutoCreatedEntities(ActiveScene());
    LUMA_USER_INFO("Scene", "Loaded scene '{}'", ProjectRelative(path));
    ReportMissingComponentPlugins();
    // Version-1 scenes carried a color per mesh; move those onto material
    // assets (in memory until the scene is saved).
    if (!legacy.empty()) MigrateLegacyMaterials(ActiveScene(), legacy, *m_materials);
    return true;
}

bool EditorScreen::SaveMaterialAssets() {
    bool ok = true;
    for (const std::string& path : m_materials->DirtyAssets()) {
        std::string err;
        if (m_materials->Save(path, &err)) {
            LUMA_USER_INFO("Materials", "Saved '{}'", path);
        } else {
            LUMA_USER_ERROR("Materials", "Failed to save '{}': {}", path, err);
            ok = false;
        }
    }
    return ok;
}

bool EditorScreen::CapturePreview(const std::string& materialPath,
                                  const std::filesystem::path& png, u32 size) {
    return m_preview && m_preview->Capture(materialPath, png, size);
}

bool EditorScreen::SelectEntityByName(const std::string& name) {
    auto view = ActiveScene().Registry().view<const NameComponent>();
    for (Entity e : view) {
        if (view.get<const NameComponent>(e).name == name) {
            SelectOnly(e);
            return true;
        }
    }
    return false;
}

std::vector<std::string> EditorScreen::UnsavedAssets() const {
    return m_materials ? m_materials->DirtyAssets() : std::vector<std::string>{};
}

void EditorScreen::RequestClose() {
    StopPlay();
    if (UnsavedAssets().empty()) {
        m_closeConfirmed = true;
    } else {
        m_closeRequested = true;
    }
}

void EditorScreen::DrawClosePrompt(Slate::Context& ui) {
    if (!m_closeRequested) return;
    const std::vector<std::string> unsaved = UnsavedAssets();
    if (unsaved.empty()) {
        m_closeRequested = false;
        m_closeConfirmed = true;
        return;
    }
    const u64 id = Slate::Context::ID("editor.unsaved");
    Slate::Theme& t = ui.theme();
    constexpr usize kListed = 6;
    const usize listed = std::min(unsaved.size(), kListed);
    const f32 lineH = 20.0f;
    const f32 height = 150.0f + lineH * static_cast<f32>(listed + (unsaved.size() > kListed));
    const Rect content = ui.BeginModal(id, "Unsaved Changes", {520.0f, height});
    f32 y = content.y;
    ui.Label({content.x, y}, "These assets have unsaved changes:", t.text);
    y += lineH + 6.0f;
    for (usize i = 0; i < listed; ++i) {
        ui.Label({content.x + 12.0f, y}, unsaved[i], t.textDim);
        y += lineH;
    }
    if (unsaved.size() > kListed) {
        ui.Label({content.x + 12.0f, y},
                 "and " + std::to_string(unsaved.size() - kListed) + " more", t.textDim);
    }
    const f32 bw = 110.0f, bh = 26.0f, gap = 8.0f;
    const f32 by = content.Bottom() + gap;
    const Rect saveRect{content.Right() - bw, by, bw, bh};
    const Rect discardRect{saveRect.x - bw - gap, by, bw, bh};
    const Rect cancelRect{discardRect.x - bw - gap, by, bw, bh};
    if (ui.Button(id ^ 0x5A5Eull, saveRect, "Save All")) {
        m_closeRequested = false;
        // Stay open when something failed to save (the errors are logged).
        m_closeConfirmed = SaveMaterialAssets();
        ui.EndModal();
    } else if (ui.Button(id ^ 0xD15Cull, discardRect, "Don't Save")) {
        m_closeRequested = false;
        m_closeConfirmed = true;
        ui.EndModal();
    } else if (ui.Button(id ^ 0xCA9Cull, cancelRect, "Cancel")) {
        m_closeRequested = false;
        ui.EndModal();
    }
}

void EditorScreen::OpenMaterialEditor(const std::string& assetPath) {
    if (OpenGraphMaterialEditor(assetPath)) return;  // node-graph editors
    const std::string id = "mated:" + assetPath;
    if (m_dock.HasPanel(id)) {
        m_dock.FocusPanel(id);
        return;
    }
    if (!m_materials->Exists(assetPath)) {
        LUMA_USER_ERROR("Materials", "Cannot open '{}': not a material", assetPath);
        return;
    }
    auto panel = std::make_unique<Editor::Panels::MaterialEditorPanel>(assetPath);
    panel->Bind(*m_materials);
    Editor::Panels::MaterialEditorPanel* raw = panel.get();
    m_materialEditors.push_back(std::move(panel));
    DockMaterialEditor(raw);
    LUMA_USER_INFO("Materials", "Opened '{}' in the Material Editor", assetPath);
}

void EditorScreen::DockMaterialEditor(Editor::Panels::MaterialEditorPanel* raw) {
    const std::string assetPath = raw->AssetPath();
    const std::string id = "mated:" + assetPath;
    m_dock.AddPanel(id, raw->Title(),
                    [this, raw](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        raw->Draw(c, r, ctx);
                    },
                    m_icons.tabDetails);
    // Asset editors open as tabs beside the viewport (the dock root panel).
    m_dock.DockWith(id, "viewport", Slate::DockDir::Center, 0.5f);
    m_dock.SetCloseHandler(id, [this, assetPath, raw] {
        if (raw->IsDirty() && m_runner.PlayScene()) {
            LUMA_USER_WARN("Materials", "Stop play before closing a modified material");
            return false;
        }
        if (raw->IsDirty()) {
            m_closingEditor = assetPath;  // ask Save / Don't Save / Cancel
            return false;
        }
        raw->ClosePopups();
        m_editorsToDelete.push_back("mated:" + assetPath);
        return true;
    });
}

void EditorScreen::LoadMaterialEditorAssets() {
    if (m_materialEditorAssetsLoaded || !m_renderer) return;
    m_materialEditorAssetsLoaded = true;
    // Content/Editor sits above Fonts/<family>/<file> (staged next to the exe).
    std::filesystem::path editorRoot = std::filesystem::path(m_uiFontPath).parent_path().parent_path().parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(editorRoot / "MaterialEditor", ec)) editorRoot = "Content/Editor";
    // Icons: amber -> deep orange gradient (the editor's icon theme).
    m_materialEditorIcons.Load(*m_renderer, editorRoot, Slate::Color{255, 196, 64, 255},
                               Slate::Color{232, 88, 12, 255});  // (RGB is a Windows macro here)
    auto watermark = std::make_unique<Slate::Font>();
    if (watermark->LoadFromFile(*m_renderer, (editorRoot / "Fonts/RubikIso/RubikIso-Regular.ttf").string(), 64.0f)) {
        m_materialWatermarkFont = std::move(watermark);
    }
    // Small sizes of the UI font so zoomed-out graph nodes stay legible.
    if (!m_uiFontPath.empty()) {
        for (f32 px : {6.0f, 7.5f, 9.0f, 10.5f, 12.0f, 14.0f}) {
            auto f = std::make_unique<Slate::Font>();
            if (f->LoadFromFile(*m_renderer, m_uiFontPath, px)) m_materialGraphFonts.push_back(std::move(f));
        }
    }
}

MaterialEditors::MaterialEditorServices EditorScreen::MakeMaterialEditorServices() {
    LoadMaterialEditorAssets();
    MaterialEditors::MaterialEditorServices s;
    s.renderPreview = [this](const MaterialEditors::PreviewRequest& q) -> TextureHandle {
        if (!m_preview) return 0;
        const int shape = std::min(static_cast<int>(q.shape), 3);
        return m_preview->Render(q.materialPath, static_cast<PreviewShape>(shape), q.yaw, q.pitch, q.width, q.height,
                                 q.sky, q.floor, q.key);
    };
    s.releasePreview = [this](u64 key) {
        if (m_preview) m_preview->Release(key);
    };
    auto listOf = [this](std::initializer_list<AssetType> types) {
        std::vector<std::string> out;
        for (AssetType type : types)
            for (const AssetData* a : m_assetRegistry.FilterByType(type))
                if (auto path = m_assetPaths.ToVirtual(a->packagePath)) out.push_back(*path);
        std::sort(out.begin(), out.end());
        return out;
    };
    s.listMaterials = [listOf] { return listOf({AssetType::Material, AssetType::MaterialInstance}); };
    s.listMeshes = [listOf] { return listOf({AssetType::Mesh}); };
    s.listTextures = [listOf] { return listOf({AssetType::Texture}); };
    s.browseToAsset = [this](const std::string& path) {
        auto ctx = BuildPanelContext();
        if (ctx.onBrowseToAsset) ctx.onBrowseToAsset(path);
    };
    s.openAsset = [this](const std::string& path) { OpenMaterialEditor(path); };
    s.compileErrors = [this](std::string_view path) { return std::string(m_bridge.Errors(path)); };
    s.pendingCompiles = [this] { return m_bridge.PendingCompiles(); };
    s.thumbnail = [this](const std::string& path) { return AssetThumbnail(path); };
    s.icons = &m_materialEditorIcons;
    s.watermarkFont = m_materialWatermarkFont.get();
    for (const auto& f : m_materialGraphFonts) s.graphFonts.push_back(f.get());
    return s;
}

bool EditorScreen::IsGraphEditor(const std::string& assetPath) const {
    return std::any_of(m_graphEditors.begin(), m_graphEditors.end(),
                       [&](const auto& e) { return e->AssetPath() == assetPath; }) ||
           std::any_of(m_instanceEditors.begin(), m_instanceEditors.end(),
                       [&](const auto& e) { return e->AssetPath() == assetPath; });
}

bool EditorScreen::SaveGraphEditor(const std::string& assetPath, std::string* error) {
    for (const auto& e : m_graphEditors)
        if (e->AssetPath() == assetPath) return e->Save(error);
    for (const auto& e : m_instanceEditors)
        if (e->AssetPath() == assetPath) return e->Save(error);
    return false;
}

bool EditorScreen::OpenGraphMaterialEditor(const std::string& assetPath, const std::string& which) {
    const bool isInstance = m_materials->GetGraphInstance(assetPath) != nullptr;
    const bool isMaterial = !isInstance && m_materials->GetGraphMaterial(assetPath) != nullptr;
    if (!isInstance && !isMaterial) return false;  // legacy asset: the Phase-1 panel
    // Default (double-click) = the Instance Editor; "material" = the node graph.
    // The node graph of an instance is its root material's.
    std::string target = assetPath;
    const bool graph = which == "material";
    if (graph && isInstance) {
        MaterialEditors::InstanceParameterModel model(assetPath, *m_materials);
        target = model.RootMaterial();
        if (target.empty() || !m_materials->GetGraphMaterial(target)) {
            LUMA_USER_ERROR("Materials", "'{}' has no graph material at the root of its parent chain", assetPath);
            return true;
        }
    }
    const std::string id = (graph ? "material:" : "instance:") + target;
    const Rect work = m_windowWorkArea;
    if (m_windows.Has(id)) {
        m_windows.Focus(id);
        return true;
    }
    MaterialEditors::MaterialEditorServices services = MakeMaterialEditorServices();
    Editor::AssetWindowDesc desc;
    desc.id = id;
    std::function<bool()> dirty;
    if (!graph) {
        auto editor = std::make_unique<MaterialEditors::MaterialInstanceEditor>(target, *m_materials, services);
        MaterialEditors::MaterialInstanceEditor* raw = editor.get();
        m_instanceEditors.push_back(std::move(editor));
        desc.draw = [raw](Slate::Context& c, const Rect& r) { raw->Draw(c, r); };
        desc.drawFloating = [raw](Slate::Context& c) { raw->DrawFloating(c); };
        desc.liveTitle = [raw] { return raw->Title(); };
        desc.title = raw->Title();
        desc.icon = m_icons.tabDetails;
        dirty = [raw] { return raw->IsDirty(); };
        desc.onClosed = [this, raw] {
            m_editorsToDelete.push_back("instance:" + raw->AssetPath());
        };
    } else {
        auto editor = std::make_unique<MaterialEditors::MaterialGraphEditor>(target, *m_materials, services);
        MaterialEditors::MaterialGraphEditor* raw = editor.get();
        m_graphEditors.push_back(std::move(editor));
        desc.draw = [raw](Slate::Context& c, const Rect& r) { raw->Draw(c, r); };
        desc.drawFloating = [raw](Slate::Context& c) { raw->DrawFloating(c); };
        desc.liveTitle = [raw] { return raw->Title(); };
        desc.title = raw->Title();
        desc.icon = m_icons.tabDetails;
        dirty = [raw] { return raw->IsDirty() || raw->HasUnappliedChanges(); };
        desc.onClosed = [this, raw] {
            m_editorsToDelete.push_back("material:" + raw->AssetPath());
        };
    }
    desc.requestClose = [this, target, id, dirty] {
        if (dirty()) {
            m_closingEditor = target;  // ask Save / Don't Save / Cancel
            m_closingWindow = id;
            return false;
        }
        return true;
    };
    m_windows.Open(std::move(desc), work);
    LUMA_USER_INFO("Materials", "Opened '{}' in the {}", target, graph ? "Material Editor" : "Material Instance Editor");
    return true;
}

void EditorScreen::DrawMaterialEditorClosePrompt(Slate::Context& ui) {
    if (m_closingEditor.empty()) return;
    const u64 id = Slate::Context::ID("editor.closematerial");
    Slate::Theme& t = ui.theme();
    const Rect content = ui.BeginModal(id, "Save Changes?", {460.0f, 150.0f});
    ui.LabelIn({content.x, content.y, content.w, 22.0f},
               "'" + m_closingEditor + "' has unsaved changes.", t.text);
    const f32 bw = 110.0f, bh = 26.0f, gap = 8.0f;
    const f32 by = content.Bottom() + gap;
    const Rect save{content.Right() - bw, by, bw, bh};
    const Rect discard{save.x - bw - gap, by, bw, bh};
    const Rect cancel{discard.x - bw - gap, by, bw, bh};
    auto close = [this] {
        const std::string path = m_closingEditor;
        for (const auto& e : m_materialEditors) {
            if (e->AssetPath() == path) e->ClosePopups();
        }
        if (!m_closingWindow.empty()) {
            m_windows.Close(m_closingWindow);  // onClosed queues the editor
        } else {
            m_dock.ClosePanel("mated:" + path);
            m_editorsToDelete.push_back("mated:" + path);
        }
        m_closingEditor.clear();
        m_closingWindow.clear();
    };
    if (ui.Button(id ^ 0x5A5Eull, save, "Save")) {
        std::string err;
        const bool saved = IsGraphEditor(m_closingEditor) ? SaveGraphEditor(m_closingEditor, &err)
                                                          : m_materials->Save(m_closingEditor, &err);
        if (saved) {
            LUMA_USER_INFO("Materials", "Saved '{}'", m_closingEditor);
            close();
        } else {
            LUMA_USER_ERROR("Materials", "Failed to save '{}': {}", m_closingEditor, err);
            m_closingEditor.clear();
            m_closingWindow.clear();
        }
        ui.EndModal();
    } else if (ui.Button(id ^ 0xD15Cull, discard, "Don't Save")) {
        // Drop the in-memory edits: reload the asset from disk.
        m_materials->Revert(m_closingEditor);
        close();
        ui.EndModal();
    } else if (ui.Button(id ^ 0xCA9Cull, cancel, "Cancel")) {
        m_closingEditor.clear();
        m_closingWindow.clear();
        ui.EndModal();
    }
}

std::string EditorScreen::UniqueMaterialPath(const std::string& desired) {
    std::string path = desired;
    for (int n = 1; m_materials->Exists(path); ++n) path = desired + "_" + std::to_string(n);
    return path;
}

void EditorScreen::CreateMaterialAsset(const std::filesystem::path& folder) {
    const auto folderPath = m_assetPaths.ToVirtual(folder / "M_NewMaterial");
    if (!folderPath) {
        LUMA_USER_ERROR("Materials", "Materials can only be created inside the project content");
        return;
    }
    const std::string path = UniqueMaterialPath(*folderPath);
    Material::Material material = Material::Material::MakeDefault();
    material.name = path.substr(path.find_last_of('/') + 1);
    m_materials->SetMaterial(path, material);
    std::string err;
    if (!m_materials->Save(path, &err)) {
        LUMA_USER_ERROR("Materials", "Could not create '{}': {}", path, err);
        m_materials->Revert(path);
        return;
    }
    m_assetRegistry.Scan();
    LUMA_USER_INFO("Materials", "Created '{}'", path);
    OpenMaterialEditor(path);
}

void EditorScreen::CreateMaterialInstanceAsset(const std::string& parent,
                                               const std::filesystem::path& folder) {
    std::string base = parent.substr(parent.find_last_of('/') + 1);
    if (base.rfind("MI_", 0) == 0) base = base.substr(3);
    if (base.rfind("M_", 0) == 0) base = base.substr(2);
    const auto desired = m_assetPaths.ToVirtual(folder / ("MI_" + base));
    if (!desired || !m_materials->Exists(parent)) {
        LUMA_USER_ERROR("Materials", "Cannot create an instance of '{}' here", parent);
        return;
    }
    Material::MaterialInstance instance;
    instance.parent = parent;
    const std::string path = m_materials->CreateInMemoryInstance(*desired, instance);
    instance.name = path.substr(path.find_last_of('/') + 1);
    m_materials->SetInstance(path, instance);
    std::string err;
    if (!m_materials->Save(path, &err)) {
        LUMA_USER_ERROR("Materials", "Could not create '{}': {}", path, err);
        m_materials->Revert(path);
        return;
    }
    m_assetRegistry.Scan();
    LUMA_USER_INFO("Materials", "Created '{}' (instance of '{}')", path, parent);
    OpenMaterialEditor(path);
}

void EditorScreen::CreateMaterialFromTextures(const std::filesystem::path& texture) {
    const auto clicked = m_assetPaths.ToVirtual(texture);
    if (!clicked) return;
    // The texture set: textures in the same folder sharing the clicked
    // texture's name prefix (the part before its role token).
    const auto probe = Material::BuildMaterialFromTextures({*clicked});
    std::string prefix = probe.suggestedName.substr(2);  // drop "M_"
    std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::vector<std::string> set;
    for (const AssetData* a :
         m_assetRegistry.Filter(AssetType::Texture, texture.parent_path(), {})) {
        if (a->packagePath.parent_path() != texture.parent_path()) continue;
        std::string name = a->assetName;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const bool inSet = name.find(prefix) != std::string::npos;
        if (!inSet && a->packagePath != texture) continue;
        if (auto vp = m_assetPaths.ToVirtual(a->packagePath)) set.push_back(*vp);
    }
    auto result = Material::BuildMaterialFromTextures(set);
    for (const std::string& ignored : result.ignored) {
        LUMA_USER_WARN("Materials", "'{}' matched no material input and was left out", ignored);
    }
    for (const std::string& redundant : result.redundant) {
        LUMA_USER_INFO("Materials", "'{}' was not used: the set already has a better map for it",
                       redundant);
    }
    const auto desired = m_assetPaths.ToVirtual(texture.parent_path() / result.suggestedName);
    if (!desired) return;
    const std::string path = UniqueMaterialPath(*desired);
    result.material.name = path.substr(path.find_last_of('/') + 1);
    m_materials->SetMaterial(path, result.material);
    std::string err;
    if (!m_materials->Save(path, &err)) {
        LUMA_USER_ERROR("Materials", "Could not create '{}': {}", path, err);
        m_materials->Revert(path);
        return;
    }
    m_assetRegistry.Scan();
    LUMA_USER_INFO("Materials", "Created '{}' from {} texture(s)", path,
                   set.size() - result.ignored.size() - result.redundant.size());
    OpenMaterialEditor(path);
}

std::optional<Editor::Panels::PlacementHit> EditorScreen::PickSurface(const Math::Vec3& origin,
                                                                     const Math::Vec3& dir) const {
    using namespace Math;
    auto& reg = const_cast<Scene&>(ActiveScene()).Registry();
    auto transformPoint = [](const Mat4& m, const Vec3& p) {
        return Vec3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
                    m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
                    m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
    };
    auto transformDir = [](const Mat4& m, const Vec3& v) {
        return Vec3(m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
                    m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
                    m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z);
    };
    std::optional<Editor::Panels::PlacementHit> best;
    f32 bestDistance = 1e30f;
    auto view = reg.view<const TransformComponent, const MeshRendererComponent>();
    for (Entity e : view) {
        if (reg.all_of<EditorHiddenComponent>(e)) continue;
        const auto& tf = view.get<const TransformComponent>(e);
        const auto& mr = view.get<const MeshRendererComponent>(e);
        Vec3 lo{-0.5f, -0.5f, -0.5f}, hi{0.5f, 0.5f, 0.5f};
        if (mr.UsesMeshAsset()) {
            const Editor::MeshBounds* bounds = m_bridge.Bounds(mr.mesh);
            if (!bounds) continue;
            lo = bounds->min;
            hi = bounds->max;
        } else if (mr.primitive == MeshPrimitive::Plane) {
            lo.y = hi.y = 0.0f;  // the plane's surface
        }
        const Mat4 model = tf.Matrix();
        const Mat4 inv = Inverse(model);
        const auto local = Editor::Panels::RayAabb(transformPoint(inv, origin), transformDir(inv, dir), lo, hi);
        if (!local) continue;
        const Vec3 world = transformPoint(model, local->point);
        const Vec3 d = world - origin;
        const f32 distance = Dot(d, d);
        if (distance < bestDistance) {
            bestDistance = distance;
            Vec3 n = transformDir(model, local->normal);
            const f32 len = std::sqrt(Dot(n, n));
            best = Editor::Panels::PlacementHit{world, len > 1e-6f ? n * (1.0f / len) : Vec3(0, 1, 0)};
        }
    }
    return best;
}

std::optional<Math::Vec3> EditorScreen::MeshDropPosition(const Math::Vec3& origin, const Math::Vec3& dir,
                                                         const std::string& mesh) {
    if (SceneRenderer* sceneRenderer = m_renderer ? m_renderer->GetSceneRenderer() : nullptr) {
        m_bridge.EnsureMesh(mesh, *sceneRenderer, m_assetPaths);
    }
    const Editor::MeshBounds* bounds = m_bridge.Bounds(mesh);
    Editor::Panels::MeshPlacementInput in;
    in.rayOrigin = origin;
    in.rayDir = dir;
    in.hit = PickSurface(origin, dir);
    if (bounds) {
        in.boundsMin = bounds->min;
        in.boundsMax = bounds->max;
    }
    in.snap = m_gizmo.Snap().translate;
    in.snapStep = m_gizmo.Snap().translateStep;
    if (!bounds) return std::nullopt;  // not loaded (or failed): nothing to rest
    return Editor::Panels::ComputeMeshPlacement(in);
}

void EditorScreen::PreviewMeshDrop(const Math::Vec3& origin, const Math::Vec3& dir,
                                   const std::string& mesh) {
    if (const auto position = MeshDropPosition(origin, dir, mesh)) {
        m_bridge.SetPlacementPreview(mesh, Math::Translate(*position));
    } else {
        m_bridge.ClearPlacementPreview();
    }
}

void EditorScreen::DropMesh(const Math::Vec3& origin, const Math::Vec3& dir, const std::string& mesh) {
    m_bridge.ClearPlacementPreview();
    // A mesh that fails to load is still placed (it draws its primitive and
    // the bridge warns once), on the surface under the cursor.
    Math::Vec3 position;
    if (const auto placed = MeshDropPosition(origin, dir, mesh)) {
        position = *placed;
    } else {
        Editor::Panels::MeshPlacementInput in;
        in.rayOrigin = origin;
        in.rayDir = dir;
        in.hit = PickSurface(origin, dir);
        in.boundsMin = {-0.5f, -0.5f, -0.5f};
        in.boundsMax = {0.5f, 0.5f, 0.5f};
        position = Editor::Panels::ComputeMeshPlacement(in);
    }
    const std::string name = UniqueEntityName(ActiveScene(), Editor::Panels::AssetNameOf(mesh));
    const Entity e = ActiveScene().CreateEntity(name);
    ActiveScene().Registry().get<TransformComponent>(e).position = position;
    auto& mr = ActiveScene().Registry().emplace<MeshRendererComponent>(e);
    Editor::Panels::AssignStaticMesh(mr, mesh);
    SelectOnly(e);
    LUMA_USER_INFO("Level", "Placed '{}'", name);
}

void EditorScreen::DropMaterial(const Math::Vec3& origin, const Math::Vec3& dir,
                                const std::string& path) {
    const Entity e = PickEntity(origin, dir);
    auto* mesh = ActiveScene().IsValid(e) ? ActiveScene().Registry().try_get<MeshRendererComponent>(e)
                                    : nullptr;
    if (!mesh) {
        LUMA_USER_WARN("Materials", "Drop '{}' onto a mesh actor to assign it", path);
        return;
    }
    Editor::Panels::AssignMaterialSlot(*mesh, 0, path);
    SelectOnly(e);
    LUMA_USER_INFO("Materials", "Assigned '{}' to '{}'", path,
                   ActiveScene().Registry().get<NameComponent>(e).name);
}

TextureHandle EditorScreen::AssetThumbnail(const std::string& assetPath) {
    if (!m_renderer) return 0;
    auto file = m_assetPaths.ToFile(assetPath, {"png", "jpg", "jpeg", "tga", "bmp", "psd", "gif",
                                                "lmat", "lmatinst", "lmesh"});
    if (!file) return 0;
    const AssetData* data = m_assetRegistry.LookupByPath(*file);
    return data ? static_cast<TextureHandle>(
                      m_contentBrowser.GetThumbnailTexture(data->id, *file, m_renderer))
                : 0;
}

std::vector<std::string> EditorScreen::ListAssets(bool materials) const {
    std::vector<std::string> out;
    auto collect = [&](AssetType type) {
        for (const AssetData* a : m_assetRegistry.Filter(type, std::nullopt, {})) {
            if (auto vp = m_assetPaths.ToVirtual(a->packagePath)) out.push_back(*vp);
        }
    };
    if (materials) {
        collect(AssetType::Material);
        collect(AssetType::MaterialInstance);
    } else {
        collect(AssetType::Texture);
    }
    if (materials) out.push_back(std::string(Material::kDefaultMaterialPath));
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void EditorScreen::RefreshThumbnailsFor(const std::string& virtualPath) {
    // Dependents are found among loaded graph assets: make sure every
    // project material / instance is loaded (each parses once, then cached).
    for (AssetType type : {AssetType::Material, AssetType::MaterialInstance}) {
        for (const AssetData* a : m_assetRegistry.Filter(type, std::nullopt, {})) {
            if (const auto v = m_assetPaths.ToVirtual(a->packagePath)) {
                if (type == AssetType::Material) m_materials->GetGraphMaterial(*v);
                else m_materials->GetGraphInstance(*v);
            }
        }
    }
    std::vector<std::string> targets = m_materials->AllDependents(virtualPath);
    targets.push_back(virtualPath);
    usize refreshed = 0;
    for (const std::string& v : targets) {
        const auto file = m_assetPaths.ToFile(v, {"lmat", "lmatinst", "lmesh"});
        const AssetData* a = file ? m_assetRegistry.LookupByPath(*file) : nullptr;
        if (!a) continue;
        ThumbnailManager::Get().DirtyThumbnail(a->id);
        m_contentBrowser.InvalidateThumbnail(a->id);
        ++refreshed;
    }
    LUMA_LOG_INFO("Thumbnails", "'{}' changed: re-rendering {} thumbnail(s)", virtualPath, refreshed);
}

void EditorScreen::RefreshAssetWatches() {
    if (!m_project) return;
    m_watchedRegistrySize = m_assetRegistry.Size();
    std::error_code ec;
    const std::filesystem::path root = m_project->AssetsDir();
    if (!std::filesystem::is_directory(root, ec)) return;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) || !IsWatchedAssetFile(it->path())) continue;
        const std::string key = it->path().lexically_normal().string();
        if (!m_watchedFiles.insert(key).second) continue;
        m_assetWatcher.Watch(it->path(), [this](const VFS::WatchedChange& change) {
            OnAssetFileChanged(change.path);  // created, modified or removed
        });
    }
}

void EditorScreen::PollAssetChanges(f32 deltaSeconds) {
    m_watchTimer += deltaSeconds;
    if (m_watchTimer < 0.5f) return;  // stat the watched files twice a second
    m_watchTimer = 0.0f;
    if (m_assetRegistry.Size() != m_watchedRegistrySize) RefreshAssetWatches();
    m_assetWatcher.Poll();
}

void EditorScreen::OnAssetFileChanged(const std::filesystem::path& file) {
    const auto virtualPath = m_assetPaths.ToVirtual(file);
    if (!virtualPath) return;
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (ext == ".lmat" || ext == ".lmatinst") {
        // A missing (deleted/renamed) file resolves to the Default Material
        // with a user warning.
        m_materials->Reload(*virtualPath);
        RefreshThumbnailsFor(*virtualPath);
    } else if (ext == ".lmesh") {
        m_bridge.OnMeshFileChanged(*virtualPath);
        if (m_preview) m_preview->OnMeshFileChanged(*virtualPath);
        RefreshThumbnailsFor(*virtualPath);
    } else {
        // Changed or removed texture: re-validate (missing -> constant, warned
        // once) and re-upload it in the viewport and the preview.
        m_materials->InvalidateTextures();
        m_bridge.OnTextureFileChanged(*virtualPath);
        if (m_preview) m_preview->OnTextureFileChanged(*virtualPath);
        RefreshThumbnailsFor(*virtualPath);
    }
}

void EditorScreen::SaveScene() {
    if (m_runner.PlayScene()) {
        LUMA_USER_WARN("Scene", "Stop play before saving the scene");
        return;
    }
    if (!m_project) return;
    if (!SaveMaterialAssets()) {
        // The scene may reference unsaved instances (e.g. migrated colors):
        // writing it now would point it at assets that do not exist.
        LUMA_USER_ERROR("Scene", "Scene not saved: fix the material save errors above first");
        return;
    }
    const std::filesystem::path path =
        m_scenePath.empty() ? m_project->StartupScenePath() : m_scenePath;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::string err;
    if (SceneSerializer::SaveToFile(ActiveScene(), path, &err)) {
        LUMA_USER_INFO("Scene", "Saved scene '{}'", ProjectRelative(path));
    } else {
        LUMA_USER_ERROR("Scene", "Failed to save scene '{}': {}", ProjectRelative(path),
                       err);
    }
}

void EditorScreen::CaptureProjectPreview() {
    if (!m_project || !m_renderer) return;
    std::error_code ec;
    const std::filesystem::path dir =
        m_project->IntermediateDir() / "Thumbnails";
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        LUMA_LOG_WARN("Editor", "failed to create project preview directory: {}",
                      ec.message());
        return;
    }
    m_renderer->CaptureFrame((dir / "ScenePreview.full.png").string());
}

void EditorScreen::FinalizeProjectPreview() {
    if (!m_project) return;
    const std::filesystem::path dir =
        m_project->IntermediateDir() / "Thumbnails";
    const std::filesystem::path fullPath = dir / "ScenePreview.full.png";
    const std::filesystem::path previewPath = dir / "ScenePreview.png";

    u32 sourceWidth = 0;
    u32 sourceHeight = 0;
    std::vector<u8> pixels;
    if (!Slate::LoadImagePixels(fullPath.string(), sourceWidth, sourceHeight,
                                pixels)) {
        return;
    }

    const Rect viewport = m_viewportPanel.Rect();
    const u32 x = std::min<u32>(sourceWidth, static_cast<u32>(
        std::max(0.0f, viewport.x)));
    const u32 y = std::min<u32>(sourceHeight, static_cast<u32>(
        std::max(0.0f, viewport.y)));
    const u32 width = std::min<u32>(sourceWidth - x, static_cast<u32>(
        std::max(1.0f, viewport.w)));
    const u32 height = std::min<u32>(sourceHeight - y, static_cast<u32>(
        std::max(1.0f, viewport.h)));
    if (width == 0 || height == 0) return;

    std::vector<u8> cropped(static_cast<usize>(width) * height * 4);
    for (u32 row = 0; row < height; ++row) {
        const u8* source = pixels.data() +
            (static_cast<usize>(y + row) * sourceWidth + x) * 4;
        u8* destination = cropped.data() + static_cast<usize>(row) * width * 4;
        std::memcpy(destination, source, static_cast<usize>(width) * 4);
    }
    stbi_write_png(previewPath.string().c_str(), static_cast<int>(width),
                   static_cast<int>(height), 4, cropped.data(),
                   static_cast<int>(width) * 4);
    std::error_code ec;
    std::filesystem::remove(fullPath, ec);
}

void EditorScreen::CreateActor(const std::string& templateId) {
    const EntityTemplate* t = EntityTemplates().Find(templateId);
    if (!t) {
        LUMA_USER_ERROR("Scene", "Cannot create '{}': no such entity template", templateId);
        return;
    }
    SelectOnly(Instantiate(ActiveScene(), *t, {m_nextNumber++}));
}

void EditorScreen::ReportMissingComponentPlugins() {
    // Components kept for plugins that are not loaded: tell the user once per
    // load which plugins the scene needs (their data stays in the scene).
    std::map<std::string, usize> byOwner;
    for (auto [e, kept] : ActiveScene().Registry().view<UnresolvedComponents>().each()) {
        for (const UnresolvedComponent& c : kept.payloads) {
            ++byOwner[c.owner.empty() ? std::string("an unknown plugin") : c.owner];
        }
    }
    for (const auto& [owner, count] : byOwner) {
        LUMA_USER_WARN("Scene",
                       "{} component(s) need {}, which is not loaded; their data is kept and "
                       "saved unchanged",
                       count, owner);
    }
}

Editor::Panels::PanelContext EditorScreen::BuildPanelContext() {
    Editor::Panels::PanelContext ctx;
    ctx.renderer = m_renderer;
    ctx.assetRegistry = &m_assetRegistry;
    ctx.scene = &ActiveScene();
    ctx.canSave = !m_runner.PlayScene();
    ctx.selected = &m_selected;
    ctx.camYaw = &m_camYaw;
    ctx.camPitch = &m_camPitch;
    ctx.camDistance = &m_camDistance;
    ctx.camTarget = &m_camTarget;
    ctx.fovY = m_fovY;
    ctx.nearZ = m_nearZ;
    ctx.farZ = m_farZ;
    ctx.view = &m_view;
    ctx.gizmoScale = &m_gizmoScale;
    ctx.gizmo = &m_gizmo;
    ctx.onCreateEntity = [this](const std::string& templateId) { CreateActor(templateId); };
    ctx.onSaveScene = [this] { SaveScene(); };
    ctx.onImportAssets = [this](const std::filesystem::path& folder) {
        ImportAssetsInto(folder);
    };
    ctx.onReimport = [this](const std::filesystem::path& asset) { ReimportAsset(asset); };
    ctx.sourceChanged = [this](const std::filesystem::path& asset) {
        return m_sourceWatch != nullptr && m_sourceWatch->IsChanged(asset);
    };
    ctx.audioPreview = m_audioPreview;
    ctx.onCreateScene = [this](const std::filesystem::path& folder) {
        CreateSceneAsset(folder);
    };
    ctx.onDeleteAsset = [this](const std::filesystem::path& path) {
        DeleteAssetPath(path);
    };
    ctx.onRevealInExplorer = [this](const std::filesystem::path& path) {
        RevealInExplorer(path);
    };
    ctx.onDuplicateEntity = [this](Entity e) { DuplicateEntity(e); };
    ctx.onDeleteEntity = [this](Entity e) { DeleteEntity(e); };
    ctx.onFocusEntity = [this](Entity e) { FocusEntity(e); };
    ctx.onCutEntities = [this](Entity e) { CutEntities(e); };
    ctx.onCopyEntities = [this](Entity e) { CopyEntities(e); };
    ctx.onPasteEntities = [this](Entity e, bool asChild) { PasteEntities(e, asChild); };
    ctx.canPasteEntities = [this] { return !m_clipboard.Empty(); };
    ctx.onCreateChild = [this](Entity e) { CreateEmptyChild(e); };
    ctx.onSelectChildren = [this](Entity e) { SelectChildren(e); };
    ctx.onMoveToRoot = [this](Entity e) { MoveToRoot(e); };
    ctx.onHideEntities = [this](Entity e) { HideEntities(e); };
    ctx.onShowAllActors = [this] { ShowAllActors(); };
    ctx.isEntityHidden = [this](Entity e) { return IsHidden(e); };
    ctx.entityHasChildren = [this](Entity e) { return !ChildrenOf(ActiveScene(), e).empty(); };
    ctx.entityIsRoot = [this](Entity e) { return ParentOf(ActiveScene(), e) == kNullEntity; };
    ctx.selection = &m_selection;
    ctx.onPickRay = [this](const Math::Vec3& origin, const Math::Vec3& dir) {
        SelectOnly(PickEntity(origin, dir));
    };
    ctx.materials = m_materials.get();
    ctx.jobs = m_jobs;
    ctx.tasks = m_tasks;
    ctx.sectionCount = [this](Entity e) -> u32 {
        const auto* mesh = ActiveScene().Registry().try_get<MeshRendererComponent>(e);
        return mesh ? m_bridge.SectionCount(*mesh) : 1u;
    };
    ctx.renderMaterialPreview = [this](const std::string& path, int shape, f32 yaw, f32 pitch,
                                       u32 width, u32 height, bool sky) -> TextureHandle {
        if (!m_preview) return 0;
        // The Material Editor panel's preview: keyed, so it re-renders only on change.
        constexpr u64 kPanelPreviewKey = 0x4D41'5450'5256'0001ull;
        return m_preview->Render(path, static_cast<PreviewShape>(std::clamp(shape, 0, 3)), yaw,
                                 pitch, width, height, sky, true, kPanelPreviewKey);
    };
    ctx.assetThumbnail = [this](const std::string& path) { return AssetThumbnail(path); };
    ctx.listTextures = [this] { return ListAssets(false); };
    ctx.listMaterials = [this] { return ListAssets(true); };
    ctx.listMeshes = [this] {
        std::vector<std::string> out;
        for (const AssetData* a : m_assetRegistry.Filter(AssetType::Mesh, std::nullopt, {})) {
            if (auto vp = m_assetPaths.ToVirtual(a->packagePath)) out.push_back(*vp);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    ctx.meshSections = [this](Entity e) {
        std::vector<Editor::Panels::MeshSlotInfo> out;
        const auto* mesh = ActiveScene().Registry().try_get<MeshRendererComponent>(e);
        if (!mesh || !mesh->UsesMeshAsset()) return out;
        if (const auto* sections = m_bridge.Sections(mesh->mesh)) {
            for (const Editor::MeshSectionInfo& s : *sections) out.push_back({s.slotName, s.defaultMaterial});
        }
        return out;
    };
    ctx.onOpenAsset = [this](const std::string& path) { OpenMaterialEditor(path); };
    ctx.onOpenAssetWith = [this](const std::string& path, const std::string& editor) {
        if (!OpenGraphMaterialEditor(path, editor)) OpenMaterialEditor(path);
    };
    ctx.virtualPathOf = [this](const std::filesystem::path& file) {
        return m_assetPaths.ToVirtual(file).value_or(std::string{});
    };
    ctx.onCreateMaterial = [this](const std::filesystem::path& folder) {
        CreateMaterialAsset(folder);
    };
    ctx.onCreateMaterialInstance = [this](const std::string& parent,
                                          const std::filesystem::path& folder) {
        CreateMaterialInstanceAsset(parent, folder);
    };
    ctx.onCreateMaterialFromTextures = [this](const std::filesystem::path& texture) {
        CreateMaterialFromTextures(texture);
    };
    ctx.onDropMaterial = [this](const Math::Vec3& origin, const Math::Vec3& dir,
                                const std::string& path) { DropMaterial(origin, dir, path); };
    ctx.onMeshDragPreview = [this](const Math::Vec3& origin, const Math::Vec3& dir,
                                   const std::string& mesh) { PreviewMeshDrop(origin, dir, mesh); };
    ctx.onDropMesh = [this](const Math::Vec3& origin, const Math::Vec3& dir,
                            const std::string& mesh) { DropMesh(origin, dir, mesh); };
    ctx.onMeshDragEnd = [this] { m_bridge.ClearPlacementPreview(); };
    ctx.onAssetRenamed = [this](const std::string& oldPath, const std::string& newPath) {
        // Unreal leaves a redirector; Luma fixes the open scene up directly.
        // Scenes that are not open keep the old path (see GOAL.md 5.6).
        const u32 changed = RetargetAssetReferences(ActiveScene(), oldPath, newPath);
        m_bridge.OnMeshFileChanged(oldPath);
        if (m_preview) m_preview->OnMeshFileChanged(oldPath);
        if (changed > 0) {
            LUMA_USER_INFO("Content", "Updated {} reference(s) to '{}' in the open scene (save it to keep them)",
                           changed, newPath);
        }
    };
    ctx.onBrowseToAsset = [this](const std::string& path) {
        if (m_materials->IsInstance(path)) {
            if (auto file = m_assetPaths.ToFile(path, {"lmatinst"})) m_contentBrowser.BrowseTo(*file);
        } else if (auto file = m_assetPaths.ToFile(path, {"lmat", "lmesh", "png", "jpg", "jpeg",
                                                          "tga", "bmp", "psd", "gif"})) {
            m_contentBrowser.BrowseTo(*file);
        }
    };
    ctx.selectedAsset = [this]() -> std::string {
        const AssetData* a = m_assetRegistry.Lookup(m_contentBrowser.Selected());
        if (!a) return {};
        return m_assetPaths.ToVirtual(a->packagePath).value_or(std::string{});
    };
    ctx.onResetCamera = [this] {
        m_camYaw = kDefaultCamYaw;
        m_camPitch = kDefaultCamPitch;
        m_camDistance = kDefaultCamDistance;
        m_camTarget = Math::Vec3(0.0f, 0.0f, 0.0f);
    };
    return ctx;
}

SceneView EditorScreen::BuildSceneView() {
    using namespace Math;
    Vec3 eye{m_camTarget.x + m_camDistance * std::cos(m_camPitch) *
                              std::sin(m_camYaw),
             m_camTarget.y + m_camDistance * std::sin(m_camPitch),
             m_camTarget.z + m_camDistance * std::cos(m_camPitch) *
                              std::cos(m_camYaw)};
    m_view = LookAt(eye, m_camTarget, Vec3(0.0f, 1.0f, 0.0f));
    // Constant on-screen gizmo size: scale with the eye-to-actor distance
    // (the fly camera moves the eye independently of the orbit distance) and
    // the field of view.
    {
        f32 dist = m_camDistance;
        if (ActiveScene().IsValid(m_selected) &&
            ActiveScene().Registry().all_of<TransformComponent>(m_selected)) {
            const Vec3 d = ActiveScene().Registry().get<TransformComponent>(m_selected).position - eye;
            dist = std::sqrt(Dot(d, d));
        }
        const f32 fovScale = std::tan(m_viewportPanel.Settings().fovYRadians * 0.5f) /
                             std::tan(0.45f);
        m_gizmoScale = std::max(dist, 0.05f) * 0.16f * fovScale;
    }

    // Materials, textures and mesh assets the scene uses are made resident
    // through the renderer's SceneRenderer; the selection draws normally
    // (the selection outline marks it).
    if (SceneRenderer* sceneRenderer = m_renderer ? m_renderer->GetSceneRenderer() : nullptr) {
        m_bridge.Sync(ActiveScene(), *sceneRenderer, *m_materials);
    }
    m_bridge.BuildInstances(ActiveScene(), m_selected, m_instances);
    m_sceneInstanceCount = static_cast<u32>(m_instances.size());
    AppendCameraModels();

    // The first directional light is the scene's sun: it drives the sky
    // atmosphere, the sun disk, cascaded shadows and the directional term in
    // every renderer. It is not added to the punctual list (it is rendered as
    // the sun instead). Point/spot lights (and any extra directional lights)
    // go into the punctual list.
    m_lights.clear();
    bool hasSun = false;
    SceneLight sun;
    auto lightView =
        ActiveScene().Registry().view<TransformComponent, LightComponent>();
    for (Entity e : lightView) {
        if (ActiveScene().Registry().all_of<EditorHiddenComponent>(e)) continue;
        const auto& tf = lightView.get<TransformComponent>(e);
        const auto& lc = lightView.get<LightComponent>(e);
        Mat4 mtx = tf.Matrix();
        Vec3 fwd = Normalize(Vec3(-mtx.m[8], -mtx.m[9], -mtx.m[10]));
        SceneLight sl;
        sl.type = static_cast<u32>(lc.type);
        sl.position = tf.position;
        sl.direction = fwd;
        // The Inspector edits attenuationRadius/attenuationPower (the physical
        // falloff controls); those drive the light's reach and falloff curve.
        sl.range = lc.attenuationRadius;
        sl.falloffExponent = lc.attenuationPower;
        sl.color = lc.color;
        sl.intensity = lc.intensity;
        sl.cosInner = std::cos(Radians(lc.innerAngleDeg));
        sl.cosOuter = std::cos(Radians(lc.outerAngleDeg));
        sl.castShadows = lc.castShadows;
        sl.shadowBias = lc.shadowBias;
        sl.normalBias = lc.normalBias;
        sl.shadowSoftness = lc.shadowSoftness;
        sl.shadowMapSize = static_cast<u32>(lc.shadowMapSize);
        sl.cascadeCount = static_cast<u32>(lc.cascadeCount);
        sl.shadowDistance = lc.shadowDistance;
        sl.cascadeSplitLambda = lc.cascadeSplitLambda;
        sl.sunDiskSizeDeg = lc.sunDiskSizeDeg;
        sl.sunDiskIntensity = lc.sunDiskIntensity;
        if (lc.type == LightType::Directional && !hasSun) {
            // First directional light = sun (drives sky + lighting + shadows).
            hasSun = true;
            sun = sl;
            continue;
        }
        m_lights.push_back(sl);
    }

    SceneView scene;
    auto envView = ActiveScene().Registry().view<const EnvironmentComponent>();
    for (Entity e : envView) {
        const auto& env = envView.get<const EnvironmentComponent>(e);
        // --- Sky (physical atmosphere) ---
        scene.sky.enabled = env.skyEnabled;
        if (hasSun) {
            scene.sky.sunDirection = sun.direction;
            scene.sky.sunColor = sun.color;
            scene.sky.sunIntensity = sun.intensity;
            scene.sky.sunDiskSizeDeg = sun.sunDiskSizeDeg;
            scene.sky.sunDiskIntensity = sun.sunDiskIntensity;
        }
        scene.sky.rayleighScattering = env.rayleighScattering;
        scene.sky.rayleighScaleHeight = env.rayleighScaleHeight;
        scene.sky.mieScattering = env.mieScattering;
        scene.sky.mieAbsorption = env.mieAbsorption;
        scene.sky.mieScaleHeight = env.mieScaleHeight;
        scene.sky.mieAnisotropy = env.mieAnisotropy;
        scene.sky.ozoneScale = env.ozoneScale;
        scene.sky.skyIntensity = env.skyIntensity;
        scene.sky.saturation = env.saturation;
        scene.sky.exposure = env.exposure;
        scene.sky.skyTint = env.skyTint;
        scene.sky.groundColor = env.groundColor;
        // --- Lighting (sun + IBL ambient) ---
        if (hasSun) {
            scene.lighting.sunDirection = sun.direction;
            scene.lighting.sunColor = sun.color;
            scene.lighting.sunIntensity = sun.intensity;
            scene.lighting.sunShadows = sun.castShadows;
            scene.lighting.shadowDistance = sun.shadowDistance;
            scene.lighting.shadowSoftness = sun.shadowSoftness;
            scene.lighting.shadowBias = sun.shadowBias;
            scene.lighting.normalBias = sun.normalBias;
            scene.lighting.cascadeSplitLambda = sun.cascadeSplitLambda;
            scene.lighting.cascadeCount = sun.cascadeCount;
        }
        scene.lighting.groundColor = env.groundColor;
        scene.lighting.iblIntensity = env.iblIntensity;
        // Analytic skylight colours (Unreal-style hemisphere): a deep zenith,
        // a brighter horizon, and the environment ground bounce.
        scene.lighting.skyZenith = Math::Vec3(
            0.14f * env.skyTint.x, 0.30f * env.skyTint.y, 0.64f * env.skyTint.z);
        scene.lighting.skyHorizon = Math::Vec3(
            0.58f * env.skyTint.x, 0.66f * env.skyTint.y, 0.80f * env.skyTint.z);
        break;
    }

    // Post-processing: the highest-priority enabled PostProcessComponent wins.
    // Volume extents/blend radius are not implemented yet, so the winner is
    // applied globally (like Unreal's "Infinite Extent (Unbound)" volumes).
    {
        const PostProcessComponent* active = nullptr;
        auto ppView = ActiveScene().Registry().view<const PostProcessComponent>();
        for (Entity e : ppView) {
            const auto& pp = ppView.get<const PostProcessComponent>(e);
            if (pp.enabled && (!active || pp.priority > active->priority)) active = &pp;
        }
        if (active) {
            const f32 w = std::clamp(active->blendWeight, 0.0f, 1.0f);
            const PostProcessParams defaults{};
            PostProcessParams& out = scene.postProcess;
            out.bloomEnabled = active->bloomEnabled;
            out.bloomIntensity = active->bloomIntensity * w;
            out.bloomThreshold = active->bloomThreshold;
            out.bloomSoftThreshold = active->bloomSoftThreshold;
            out.bloomTint = active->bloomTint;
            out.toneMapper =
                static_cast<PostProcessParams::ToneMapper>(active->toneMapper);
            // blendWeight fades exposure toward the renderer default.
            out.exposure = defaults.exposure +
                           (active->exposure - defaults.exposure) * w;
            out.autoExposure = active->autoExposure;
            out.autoExposureSpeed = active->autoExposureSpeed;
            out.autoExposureMinLuminance = active->autoExposureMin;
            out.autoExposureMaxLuminance = active->autoExposureMax;
        }
    }

    // Viewport toolbar settings (view mode, show flags, FOV).
    const auto& vp = m_viewportPanel.Settings();
    m_fovY = vp.fovYRadians;
    scene.viewMode = vp.viewMode;
    if (!vp.showBloom) scene.postProcess.bloomEnabled = false;
    if (!vp.showEyeAdaptation) scene.postProcess.autoExposure = false;

    scene.grid.enabled = vp.showGrid;
    scene.grid.cellSize = 1.0f;
    scene.grid.majorEvery = 10;
    scene.grid.fadeStart = m_camDistance * 3.0f;
    scene.grid.fadeEnd = m_camDistance * 60.0f;
    scene.view = m_view;
    scene.fovYRadians = m_fovY;
    scene.nearZ = m_nearZ;
    scene.farZ = m_farZ;
    scene.instances = m_instances.data();
    scene.instanceCount = static_cast<u32>(m_instances.size());
    scene.lights = m_lights.data();
    scene.lightCount = static_cast<u32>(m_lights.size());

    // Overlay lines: the gizmo plus DebugDraw overlay shapes; depth-tested
    // DebugDraw shapes go to SceneView::lines (editor tools and PIE systems).
    m_overlayLines.clear();
    if (ActiveScene().IsValid(m_selected) &&
        ActiveScene().Registry().all_of<TransformComponent>(m_selected)) {
        const auto& tf = ActiveScene().Registry().get<TransformComponent>(m_selected);
        GizmoTransform gt{tf.position, tf.rotationEuler, tf.scale};
        const auto& lines = m_gizmo.BuildLines(gt, m_gizmoScale, m_view);
        m_overlayLines.assign(lines.begin(), lines.end());

        // Influence/frustum gizmo for the selected camera or light. Drawn as
        // overlay DebugDraw shapes (picked up just below), from the real
        // component data so it matches what the renderer uses.
        const Mat4 world = tf.Matrix();
        const Vec3 pos = tf.position;
        const Vec3 fwd = Normalize(Vec3(-world.m[8], -world.m[9], -world.m[10]));
        const Vec3 right = Normalize(Vec3(world.m[0], world.m[1], world.m[2]));
        auto& dd = DebugDrawer();
        using Overlay = DebugDraw::Mode;
        auto& reg = ActiveScene().Registry();
        if (reg.all_of<CameraComponent>(m_selected)) {
            const auto& cam = reg.get<CameraComponent>(m_selected);
            // Preview aspect: the game camera has no fixed aspect until it
            // renders, so use a 16:9 default (see design doc).
            const Mat4 invVP = world * Inverse(cam.ProjectionMatrix(16.0f / 9.0f));
            dd.Frustum(invVP, Vec3{0.8f, 0.8f, 0.85f}, Overlay::Overlay);
        }
        if (reg.all_of<LightComponent>(m_selected)) {
            const auto& lc = reg.get<LightComponent>(m_selected);
            // Lights shine along the entity's local +Z; the gizmos follow the
            // emitted light, not the camera-style -Z forward.
            const Vec3 aim = fwd * -1.0f;
            switch (lc.type) {
                case LightType::Spot:
                    dd.Cone(pos, aim, lc.range, lc.outerAngleDeg, lc.color, Overlay::Overlay);
                    dd.Cone(pos, aim, lc.range, lc.innerAngleDeg, lc.color, Overlay::Overlay);
                    break;
                case LightType::Point:
                    dd.Sphere(pos, lc.attenuationRadius, lc.color, Overlay::Overlay);
                    break;
                case LightType::Tube:
                    dd.Capsule(pos - right * (lc.length * 0.5f), pos + right * (lc.length * 0.5f),
                               lc.attenuationRadius, lc.color, Overlay::Overlay);
                    break;
                case LightType::Directional: {
                    // Aim arrow: a shaft plus four angled head lines.
                    const f32 len = 2.0f;
                    const Vec3 tip = pos + aim * len;
                    dd.Line(pos, tip, lc.color, Overlay::Overlay);
                    const Vec3 up = Normalize(Vec3(world.m[4], world.m[5], world.m[6]));
                    dd.Line(tip, tip - aim * 0.4f + right * 0.2f, lc.color, Overlay::Overlay);
                    dd.Line(tip, tip - aim * 0.4f - right * 0.2f, lc.color, Overlay::Overlay);
                    dd.Line(tip, tip - aim * 0.4f + up * 0.2f, lc.color, Overlay::Overlay);
                    dd.Line(tip, tip - aim * 0.4f - up * 0.2f, lc.color, Overlay::Overlay);
                    break;
                }
            }
        }
    }
    const auto& debugOverlay = DebugDrawer().Lines(DebugDraw::Mode::Overlay);
    m_overlayLines.insert(m_overlayLines.end(), debugOverlay.begin(), debugOverlay.end());
    if (!m_overlayLines.empty()) {
        scene.overlayLines = m_overlayLines.data();
        scene.overlayLineVertexCount = static_cast<u32>(m_overlayLines.size());
    }
    const auto& debugLines = DebugDrawer().Lines(DebugDraw::Mode::DepthTested);
    m_debugLines.assign(debugLines.begin(), debugLines.end());
    if (!m_debugLines.empty()) {
        scene.lines = m_debugLines.data();
        scene.lineVertexCount = static_cast<u32>(m_debugLines.size());
    }
    // Shapes are per frame: everything added since the last view is in this one.
    DebugDrawer().Clear();
    return scene;
}

void EditorScreen::SetEditorIcons(const IconSet& icons) {
    m_icons = icons;
    m_viewportPanel.SetIcons(icons.viewport);
    m_consolePanel.SetBadgeIcons(icons.error, icons.warning);
    const std::pair<const char*, TextureHandle> tabs[] = {
        {"TabOutliner", icons.tabOutliner}, {"TabDetails", icons.tabDetails},
        {"TabViewport", icons.tabViewport}, {"TabContent", icons.tabContent},
        {"TabOutputLog", icons.tabOutputLog},
    };
    for (const auto& [name, texture] : tabs) {
        if (Editor::EditorImages().Contains(name)) continue;  // set once per editor
        m_uiRegistrations.Add(
            Editor::EditorImages().Register(name, texture, Slate::Icon::None, "Engine"));
    }
}

void EditorScreen::BeginPlay() {
    if (m_runner.PlayScene()) {
        m_runner.SetPaused(false);
        return;
    }
    std::string error;
    if (!m_runner.Begin(m_authoringScene, m_simulation, &error)) {
        LUMA_USER_ERROR("Scene", "Cannot start Play in Editor: {}", error);
        return;
    }
    m_authoringSelected = m_selected;
    m_authoringSelection = m_selection;
    m_authoringClipboard = std::move(m_clipboard);
    m_authoringNextNumber = m_nextNumber;
    m_clipboard.Clear();
    std::erase_if(m_selection, [this](Entity e) { return !ActiveScene().IsValid(e); });
    if (!ActiveScene().IsValid(m_selected)) m_selected = kNullEntity;
    m_inspectorPanel.ClosePopups();
    m_outlinerPanel.ClearSelection();
    m_bridge.ClearPlacementPreview();
    LUMA_LOG_INFO("Scene", "Started Play in Editor");
}

void EditorScreen::StopPlay() {
    if (!m_runner.PlayScene()) return;
    m_inspectorPanel.ClosePopups();
    m_outlinerPanel.ClearSelection();
    m_bridge.ClearPlacementPreview();
    m_runner.End();
    m_selection = std::move(m_authoringSelection);
    std::erase_if(m_selection, [this](Entity e) { return !m_authoringScene.IsValid(e); });
    m_selected = m_authoringScene.IsValid(m_authoringSelected) ? m_authoringSelected : kNullEntity;
    m_clipboard = std::move(m_authoringClipboard);
    m_nextNumber = m_authoringNextNumber;
    LUMA_LOG_INFO("Scene", "Stopped Play in Editor");
}

void EditorScreen::RegisterEditorUi() {
    using Editor::MenuEntryDesc;
    using Editor::PanelDesc;
    Editor::MenuRegistry& menus = Editor::Menus();
    Editor::PanelRegistry& panels = Editor::EditorPanels();
    RegistrationSet& reg = m_uiRegistrations;

    // --- Menu bar ---------------------------------------------------------------
    reg.Add(menus.RegisterMenu({"File", "File", 0, "Engine"}));
    reg.Add(menus.RegisterMenu({"Edit", "Edit", 100, "Engine"}));
    reg.Add(menus.RegisterMenu({"Assets", "Assets", 200, "Engine"}));
    reg.Add(menus.RegisterMenu({"Tools", "Tools", 800, "Engine"}));  // plugin tools
    reg.Add(menus.RegisterMenu({"Window", "Window", 900, "Engine"}));

    auto entry = [](std::string id, std::string menu, std::string section, std::string label,
                    std::string icon, i32 order, std::function<void()> run) {
        MenuEntryDesc e;
        e.id = std::move(id);
        e.menu = std::move(menu);
        e.section = std::move(section);
        e.label = std::move(label);
        e.icon = std::move(icon);
        e.order = order;
        e.owner = "Engine";
        e.execute = std::move(run);
        return e;
    };
    auto hasProject = [this] { return m_project.has_value(); };
    auto hasSelection = [this] { return ActiveScene().IsValid(m_selected); };

    {
        MenuEntryDesc e = entry("File.SaveScene", "File", "Scene", "Save Scene", "Save", 0,
                                [this] { SaveScene(); });
        e.tooltip = "Write the open scene to disk.";
        e.shortcut = "Ctrl+S";
        e.isEnabled = [this] { return m_project.has_value() && !m_runner.PlayScene(); };
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    {
        MenuEntryDesc e = entry("File.ShowProject", "File", "Project", "Show Project in Explorer",
                                "Browse", 100, [this] {
                                    if (m_project) {
                                        RevealInExplorer(m_project->AssetsDir().parent_path());
                                    }
                                });
        e.isEnabled = hasProject;
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    // Cut / Copy keep one-per-scene holders (e.g. the Environment) in the scene.
    auto canCopySelection = [this] {
        return ActiveScene().IsValid(m_selected) && !OnePerSceneComponentOf(ActiveScene(), m_selected);
    };
    {
        MenuEntryDesc e = entry("Edit.Cut", "Edit", "Selection", "Cut", "", -30,
                                [this] { CutEntities(m_selected); });
        e.shortcut = "Ctrl+X";
        e.isEnabled = canCopySelection;
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    {
        MenuEntryDesc e = entry("Edit.Copy", "Edit", "Selection", "Copy", "", -20,
                                [this] { CopyEntities(m_selected); });
        e.shortcut = "Ctrl+C";
        e.isEnabled = canCopySelection;
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    {
        MenuEntryDesc e = entry("Edit.Paste", "Edit", "Selection", "Paste", "", -10, [this] {
            PasteEntities(ActiveScene().IsValid(m_selected) ? m_selected : kNullEntity, false);
        });
        e.shortcut = "Ctrl+V";
        e.tooltip = "Paste as a sibling of the selection, or at the scene root.";
        e.isEnabled = [this] { return !m_clipboard.Empty(); };
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    {
        MenuEntryDesc e = entry("Edit.EditorPreferences", "Edit", "Configuration",
                                "Editor Preferences", "Gear", 190,
                                [this] { m_preferencesOpen = true; });
        e.tooltip = "Your own editor settings (all projects).";
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    {
        MenuEntryDesc e = entry("Edit.Duplicate", "Edit", "Selection", "Duplicate", "Duplicate", 0,
                                [this] { DuplicateEntity(m_selected); });
        e.shortcut = "Ctrl+D";
        e.isEnabled = [this] {
            return ActiveScene().IsValid(m_selected) && !OnePerSceneComponentOf(ActiveScene(), m_selected);
        };
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    {
        MenuEntryDesc e = entry("Edit.Delete", "Edit", "Selection", "Delete", "Trash", 10,
                                [this] { DeleteEntity(m_selected); });
        e.shortcut = "Delete";
        e.isEnabled = hasSelection;
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    {
        MenuEntryDesc e = entry("Edit.Focus", "Edit", "Selection", "Focus in Viewport", "Focus", 20,
                                [this] { FocusEntity(m_selected); });
        e.shortcut = "F";
        e.isEnabled = hasSelection;
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    {
        MenuEntryDesc e = entry("Assets.Import", "Assets", "Content", "Import...", "Import", 0,
                                [this] { ImportAssetsInto(m_contentBrowser.CurrentFolder()); });
        e.tooltip = "Import files into the open Content Browser folder.";
        e.isEnabled = hasProject;
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    {
        MenuEntryDesc e = entry("Assets.Refresh", "Assets", "Content", "Refresh Content",
                                "Refresh", 10, [this] {
                                    m_assetRegistry.Scan();
                                    if (m_sourceWatch) m_sourceWatch->InvalidateAll();
                                });
        e.tooltip = "Rescan the project's content from disk.";
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    {
        MenuEntryDesc e = entry("Edit.ProjectSettings", "Edit", "Configuration",
                                "Project Settings...", "Gear", 200,
                                [this] { m_projectSettings.Open(); });
        e.tooltip = "Settings stored in this project's Config folder.";
        e.isEnabled = hasProject;
        reg.Add(menus.RegisterEntry(std::move(e)));
    }
    auto play = entry("Tools.Play", "Tools", "Simulation", "Play", "Play", 0, [this] { BeginPlay(); });
    play.isEnabled = [this] { return !m_runner.PlayScene() || m_runner.Paused(); };
    reg.Add(menus.RegisterEntry(std::move(play)));
    auto pause = entry("Tools.Pause", "Tools", "Simulation", "Pause / Resume", "Pause", 1,
                       [this] { m_runner.SetPaused(!m_runner.Paused()); });
    pause.isEnabled = [this] { return m_runner.PlayScene() != nullptr; };
    reg.Add(menus.RegisterEntry(std::move(pause)));
    auto step = entry("Tools.Step", "Tools", "Simulation", "Single Step", "Play", 2, [this] { m_runner.Step(); });
    step.isEnabled = [this] { return m_runner.Paused(); };
    reg.Add(menus.RegisterEntry(std::move(step)));
    auto stop = entry("Tools.Stop", "Tools", "Simulation", "Stop", "Stop", 3, [this] { StopPlay(); });
    stop.isEnabled = [this] { return m_runner.PlayScene() != nullptr; };
    reg.Add(menus.RegisterEntry(std::move(stop)));
    reg.Add(Editor::Panels::RegisterSimulationPage(Editor::SettingsPages(), m_simulation));
    reg.Add(Editor::Panels::RegisterDisplayPage(Editor::SettingsPages(), m_graphics.display, [this] {
        return m_activePresentMode ? m_activePresentMode() : std::string{};
    }));
    reg.Add(Editor::Panels::RegisterRenderingPage(Editor::SettingsPages(), m_graphics.rendering));
    reg.Add(Editor::Panels::RegisterMapsAndModesPage(
        Editor::SettingsPages(), m_mapsAndModes, [this] {
            // Every scene file under Assets/, as a project-relative path.
            std::vector<std::string> scenes;
            if (!m_project) return scenes;
            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(m_project->AssetsDir(), ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (it->is_regular_file(ec) && it->path().extension() == Project::kSceneExtension) {
                    scenes.push_back(
                        std::filesystem::relative(it->path(), m_project->RootDir(), ec)
                            .generic_string());
                }
            }
            std::sort(scenes.begin(), scenes.end());
            return scenes;
        }));
    {
        Editor::MenuSectionDesc s;
        s.id = "Window.Panels";
        s.menu = "Window";
        s.section = "Panels";
        s.owner = "Engine";
        s.order = 0;
        s.fill = [this](Slate::MenuBuilder& b) { m_panelHost->FillWindowMenu(b); };
        reg.Add(menus.RegisterSection(std::move(s)));
    }
    {
        MenuEntryDesc e = entry("Window.ResetLayout", "Window", "Layout", "Reset Layout", "Reset",
                                100, [this] {
                                    m_panelHost->ResetLayout();
                                    // Asset editor tabs survive a layout reset.
                                    for (const auto& editor : m_materialEditors) {
                                        DockMaterialEditor(editor.get());
                                    }
                                });
        e.tooltip = "Restore the default panel arrangement.";
        reg.Add(menus.RegisterEntry(std::move(e)));
    }

    // --- Dock panels (registration order = default layout build order) ----------
    auto panel = [](std::string id, std::string title, std::string icon, std::string tooltip,
                    std::string nextTo, Slate::DockDir dir, f32 ratio,
                    std::function<void(Slate::Context&, const Rect&,
                                       Editor::Panels::PanelContext&)> draw) {
        PanelDesc p;
        p.id = std::move(id);
        p.title = std::move(title);
        p.icon = std::move(icon);
        p.tooltip = std::move(tooltip);
        p.owner = "Engine";
        p.dockNextTo = std::move(nextTo);
        p.dockDir = dir;
        p.dockRatio = ratio;
        p.draw = std::move(draw);
        return p;
    };
    {
        PanelDesc viewport = panel("viewport", "Viewport", "TabViewport", "The 3D scene view.", "",
                                   Slate::DockDir::Center, 1.0f,
                                   [this](Slate::Context& c, const Rect& r,
                                          Editor::Panels::PanelContext& ctx) {
                                       m_viewportPanel.Draw(c, r, ctx);
                                   });
        viewport.closable = false;  // the dock root; asset editors tab beside it
        reg.Add(panels.Register(std::move(viewport)));
    }
    reg.Add(panels.Register(panel(
        "console", "Console", "TabOutputLog", "Project messages and engine logs.", "viewport",
        Slate::DockDir::Down, 0.28f,
        [this](Slate::Context& c, const Rect& r, Editor::Panels::PanelContext& ctx) {
            m_consolePanel.Draw(c, r, ctx);
        })));
    reg.Add(panels.Register(panel(
        "outliner", "World Outliner", "TabOutliner", "Every object in the scene.", "viewport",
        Slate::DockDir::Left, 0.2f,
        [this](Slate::Context& c, const Rect& r, Editor::Panels::PanelContext& ctx) {
            m_outlinerPanel.Draw(c, r, ctx);
        })));
    reg.Add(panels.Register(panel(
        "inspector", "Inspector", "TabDetails", "Components of the selected object.", "viewport",
        Slate::DockDir::Right, 0.24f,
        [this](Slate::Context& c, const Rect& r, Editor::Panels::PanelContext& ctx) {
            m_inspectorPanel.Draw(c, r, ctx);
        })));
    reg.Add(panels.Register(panel(
        "content", "Content Browser", "TabContent", "The project's assets.", "outliner",
        Slate::DockDir::Down, 0.45f,
        [this](Slate::Context& c, const Rect& r, Editor::Panels::PanelContext& ctx) {
            m_contentBrowser.Draw(c, r, ctx);
        })));
}

std::filesystem::path EditorScreen::LayoutStatePath() const {
    if (!m_project) return {};
    return m_project->RootDir() / "Saved" / "Config" / "EditorLayout.json";
}

void EditorScreen::LoadPanelLayout() {
    const std::filesystem::path path = LayoutStatePath();
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return;
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    const auto doc = ParseJson(text, &error);
    if (!doc) {
        LUMA_USER_WARN("Editor", "Ignoring the saved panel layout '{}': {}",
                       ProjectRelative(path), error);
        return;
    }
    if (const SerialValue* panels = doc->Find("panels")) m_panelHost->LoadState(*panels);
}

void EditorScreen::SavePanelLayout() {
    const std::filesystem::path path = LayoutStatePath();
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    SerialValue doc = SerialValue::MakeObject();
    doc["version"] = 1;
    doc["panels"] = m_panelHost->SaveState();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !(out << WriteJson(doc, true))) {
        LUMA_USER_WARN("Editor", "Could not save the panel layout to '{}'",
                       ProjectRelative(path));
    }
}

Renderer2::DeferredSceneView EditorScreen::BuildDeferredSceneView(u32 width,
                                                                u32 height,
                                                                f32 deltaSeconds) {
    using namespace Math;
    
    PollAssetChanges(deltaSeconds);
    // Build the RHI scene view first. Store it in a member so its instance /
    // light pointers stay valid for the lifetime of the returned view.
    m_deferredSceneSource = BuildSceneView();
    const SceneView& rhiScene = m_deferredSceneSource;
    
    // Convert to Renderer2 DeferredSceneView
    Renderer2::DeferredSceneView deferredScene;
    deferredScene.cameraPosition = Vec3(m_camTarget.x + m_camDistance * std::cos(m_camPitch) * std::sin(m_camYaw),
                                       m_camTarget.y + m_camDistance * std::sin(m_camPitch),
                                       m_camTarget.z + m_camDistance * std::cos(m_camPitch) * std::cos(m_camYaw));
    deferredScene.cameraDirection = Normalize(m_camTarget - deferredScene.cameraPosition);
    deferredScene.viewMatrix = m_view;

    const u32 vpW = width > 0 ? width : 1;
    const u32 vpH = height > 0 ? height : 1;
    deferredScene.width = vpW;
    deferredScene.height = vpH;
    // Vertical FOV is fixed; aspect comes from the live viewport rect so
    // meshes keep their proportions when the panel is resized.
    deferredScene.projectionMatrix =
        PerspectiveForViewport(m_fovY, static_cast<f32>(vpW),
                               static_cast<f32>(vpH), m_nearZ, m_farZ);
    deferredScene.viewProjectionMatrix =
        deferredScene.projectionMatrix * deferredScene.viewMatrix;
    deferredScene.fov = m_fovY;
    deferredScene.nearPlane = m_nearZ;
    deferredScene.farPlane = m_farZ;
    deferredScene.time = 0.0f; // TODO: Pass actual time
    // Clamp so a hitch (or a long first frame) doesn't jump adaptation.
    deferredScene.deltaTime = std::clamp(deltaSeconds, 0.0f, 0.25f);
    
    // Set pointers to RHI scene data
    deferredScene.sceneData = &rhiScene;
    deferredScene.lightingParams = &rhiScene.lighting;

    return deferredScene;
}

void EditorScreen::EnsureCameraModel() {
    if (m_cameraModelLoaded || !m_renderer) return;
    SceneRenderer* sr = m_renderer->GetSceneRenderer();
    if (!sr) return;  // renderer not ready yet; retry next frame
    m_cameraModelLoaded = true;

    // Content/Editor sits above Fonts/<family>/<file> (staged next to the exe);
    // the camera model lives in Content/Editor/Meshes/SM_Camera.lmesh.
    std::error_code ec;
    std::filesystem::path editorRoot =
        std::filesystem::path(m_uiFontPath).parent_path().parent_path().parent_path();
    std::filesystem::path meshPath = editorRoot / "Meshes" / "SM_Camera.lmesh";
    if (!std::filesystem::exists(meshPath, ec)) meshPath = "Content/Editor/Meshes/SM_Camera.lmesh";

    std::optional<LumaMeshData> lmd = LumaMeshIO::ReadMesh(meshPath);
    if (!lmd) {
        LUMA_LOG_WARN("Editor", "Camera model not found at {}", meshPath.string());
        return;
    }
    MeshData md = ConvertLumaMesh(*lmd);
    m_cameraModelMesh = sr->UploadMesh(0xCA3E4D0D0EULL, md);

    // Each section's material name is an "rrggbb" base colour (baked per colour
    // bucket). Parse them so AppendCameraModels can assign flat materials.
    m_cameraModelSections = static_cast<u32>(md.sections.size());
    m_cameraModelColors.clear();
    for (const MeshSection& s : md.sections) {
        Math::Vec3 c{0.3f, 0.3f, 0.3f};
        if (s.materialName.size() == 6) {
            auto hx = [&](usize i) {
                return static_cast<f32>(std::stoi(s.materialName.substr(i, 2), nullptr, 16)) / 255.0f;
            };
            c = {hx(0), hx(2), hx(4)};
        }
        m_cameraModelColors.push_back(c);
    }
}

void EditorScreen::AppendCameraModels() {
    if (m_runner.PlayScene()) return;  // hidden in game / during play
    EnsureCameraModel();
    if (m_cameraModelMesh == kInvalidHandle) return;
    SceneRenderer* sr = m_renderer ? m_renderer->GetSceneRenderer() : nullptr;
    // Resolve the per-section flat materials once (cached by colour thereafter).
    std::array<MaterialHandle, kMaxSectionMaterials> mats{};
    const u32 slots = std::min(m_cameraModelSections, kMaxSectionMaterials);
    for (u32 s = 0; s < slots; ++s) {
        mats[s] = (sr && m_materials)
                      ? m_bridge.EnsureFlatMaterial(*sr, *m_materials, m_cameraModelColors[s])
                      : kDefaultMaterialHandle;
    }
    auto& reg = ActiveScene().Registry();
    // The source model is ~0.8 m; shrink it to a gizmo-sized object so it
    // doesn't dwarf scene geometry. Scale is applied in the camera's local
    // space (prepended) so it shrinks about the camera origin.
    constexpr f32 kModelScale = 0.30f;
    Math::Mat4 scale = Math::Mat4::Identity();
    scale.m[0] = scale.m[5] = scale.m[10] = kModelScale;
    auto view = reg.view<const TransformComponent, const CameraComponent>();
    for (Entity e : view) {
        if (reg.all_of<EditorHiddenComponent>(e)) continue;
        SceneInstance inst;
        inst.model = reg.get<const TransformComponent>(e).Matrix() * scale;
        inst.mesh = m_cameraModelMesh;
        inst.sectionCount = m_cameraModelSections;
        for (u32 s = 0; s < slots; ++s) inst.sectionMaterials[s] = mats[s];
        inst.selected = (e == m_selected);
        m_instances.push_back(inst);
    }
}

bool EditorScreen::BuildCameraPreviewView(Renderer2::DeferredSceneView& out, u32 width,
                                          u32 height, std::string* name) {
    using namespace Math;
    if (!ActiveScene().IsValid(m_selected)) return false;
    auto& reg = ActiveScene().Registry();
    if (!reg.all_of<TransformComponent, CameraComponent>(m_selected)) return false;

    const auto& tf = reg.get<TransformComponent>(m_selected);
    const auto& cam = reg.get<CameraComponent>(m_selected);

    // Reuse this frame's scene content, but strip the editor grid and overlay/
    // debug lines so the preview reads like the game view. The shallow copy
    // keeps the instance/light pointers (EditorScreen members, valid this frame).
    m_cameraPreviewSceneSource = m_deferredSceneSource;
    // Drop the editor camera models (appended after the real instances) so a
    // camera never sees its own or other cameras' gizmo meshes.
    m_cameraPreviewSceneSource.instanceCount = m_sceneInstanceCount;
    m_cameraPreviewSceneSource.grid.enabled = false;
    m_cameraPreviewSceneSource.overlayLines = nullptr;
    m_cameraPreviewSceneSource.overlayLineVertexCount = 0;
    m_cameraPreviewSceneSource.lines = nullptr;
    m_cameraPreviewSceneSource.lineVertexCount = 0;

    const u32 vpW = width > 0 ? width : 1;
    const u32 vpH = height > 0 ? height : 1;
    const f32 aspect = static_cast<f32>(vpW) / static_cast<f32>(vpH);
    const Mat4 world = tf.Matrix();
    const Mat4 view = Inverse(world);

    out.cameraPosition = tf.position;
    out.cameraDirection = Normalize(Vec3(-world.m[8], -world.m[9], -world.m[10]));
    out.viewMatrix = view;
    out.projectionMatrix = cam.ProjectionMatrix(aspect);
    out.viewProjectionMatrix = out.projectionMatrix * view;
    out.width = vpW;
    out.height = vpH;
    out.fov = Radians(cam.fovYDegrees);
    out.nearPlane = cam.nearZ;
    out.farPlane = cam.farZ;
    out.time = 0.0f;
    out.deltaTime = 0.0f;
    out.sceneData = &m_cameraPreviewSceneSource;
    out.lightingParams = &m_cameraPreviewSceneSource.lighting;

    if (name != nullptr) {
        *name = reg.all_of<NameComponent>(m_selected)
                    ? reg.get<NameComponent>(m_selected).name
                    : std::string("Camera");
    }
    return true;
}

namespace {

std::string FamilyLabel(std::string_view name) {
    if (name.find("Model") != std::string_view::npos) return "Models";
    if (name.find("Image") != std::string_view::npos) return "Images";
    if (name.find("Audio") != std::string_view::npos) return "Audio";
    return std::string(name);
}

std::vector<char> ImportFileFilter(const Import::TranslatorRegistry& registry) {
    std::vector<std::pair<std::string, std::string>> families;
    std::string all;
    for (const std::string& ext : registry.SupportedExtensions()) {
        const Import::ITranslator* translator = registry.Find(std::filesystem::path("import").concat(ext));
        const std::string label = FamilyLabel(translator != nullptr ? translator->Name() : "Other");
        const std::string pattern = "*" + ext;
        if (!all.empty()) all += ";";
        all += pattern;
        auto it = std::find_if(families.begin(), families.end(),
                               [&](const auto& family) { return family.first == label; });
        if (it == families.end()) families.push_back({label, pattern});
        else {
            it->second += ";";
            it->second += pattern;
        }
    }
    std::vector<char> filter;
    auto append = [&](const std::string& text) {
        filter.insert(filter.end(), text.begin(), text.end());
        filter.push_back('\0');
    };
    append("All Supported");
    append(all);
    for (const auto& family : families) {
        append(family.first);
        append(family.second);
    }
    filter.push_back('\0');
    return filter;
}

void CollectImportFiles(const std::filesystem::path& path, const Import::TranslatorRegistry& registry,
                        std::vector<std::filesystem::path>& out) {
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        for (std::filesystem::recursive_directory_iterator it(path, error), end; it != end && !error; it.increment(error)) {
            if (!it->is_regular_file(error)) continue;
            CollectImportFiles(it->path(), registry, out);
        }
        return;
    }
    if (!std::filesystem::is_regular_file(path, error)) return;
    if (registry.Find(path) != nullptr) out.push_back(path);
    else LUMA_USER_WARN("Import", "'{}' is not a supported import format", path.filename().string());
}

}  // namespace

void EditorScreen::HandleDroppedFiles(const std::vector<std::filesystem::path>& files) {
    if (files.empty() || !m_project) return;
    std::filesystem::path target = m_contentBrowser.ImportDropFolder();
    if (target.empty()) target = m_contentBrowser.CurrentFolder();
    if (target.empty()) target = m_project->AssetsDir();
    auto& registry = Import::Translators();
    std::vector<std::filesystem::path> supported;
    for (const std::filesystem::path& file : files) CollectImportFiles(file, registry, supported);
    OpenImport(supported, target, false);
}

void EditorScreen::ImportAssetsInto(const std::filesystem::path& folder) {
    if (!m_project) return;
    std::filesystem::path target = folder.empty() ? m_project->AssetsDir() : folder;
    auto& registry = Import::Translators();
    const std::vector<char> filter = ImportFileFilter(registry);
    std::vector<std::filesystem::path> picked;
#ifdef _WIN32
    {
        char buffer[16384] = {0};
        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = filter.data();
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = static_cast<DWORD>(sizeof(buffer));
        ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST;
        ofn.lpstrTitle = "Import Assets";
        if (GetOpenFileNameA(&ofn)) {
            const char* p = buffer;
            std::string first(p);
            p += first.size() + 1;
            if (*p == '\0') picked.push_back(first);
            else {
                const std::filesystem::path dir(first);
                while (*p != '\0') {
                    picked.push_back(dir / p);
                    p += std::strlen(p) + 1;
                }
            }
        }
    }
#endif
    std::vector<std::filesystem::path> supported;
    for (const std::filesystem::path& file : picked) CollectImportFiles(file, registry, supported);
    OpenImport(supported, target, false);
}

struct EditorScreen::PendingTranslate {
    std::unique_ptr<Import::ImportSession> session;
    std::string error;
    std::filesystem::path folder;
    bool reimport = false;
    std::atomic<bool> done{false};
    Tasks::TaskToken token;
};

void EditorScreen::SetBackgroundServices(Jobs::JobSystem* jobs, Tasks::TaskManager* tasks) {
    m_jobs = jobs;
    m_tasks = tasks;
    m_sourceWatch = std::make_unique<Import::SourceWatch>(jobs, tasks);
    // Mesh LODs for the level viewport, built in the background and cached
    // per project (derived data; safe to delete).
    if (m_project) m_bridge.EnableMeshLods(jobs, m_project->RootDir() / "Intermediate" / "MeshLods");
}

void EditorScreen::BeginTranslate(std::string title, std::filesystem::path folder, bool reimport,
                                  std::function<std::unique_ptr<Import::ImportSession>(std::string*)> work) {
    if (m_translate) {
        LUMA_USER_WARN("Import", "Still reading the previous files; try again when it finishes");
        return;
    }
    if (m_importSession && m_importSession->Running()) {
        LUMA_USER_WARN("Import", "An import is already running");
        return;
    }
    auto pending = std::make_shared<PendingTranslate>();
    pending->folder = std::move(folder);
    pending->reimport = reimport;
    if (m_jobs == nullptr) {
        pending->session = work(&pending->error);
        pending->done = true;
        m_translate = std::move(pending);
        PollTranslate();
        return;
    }
    if (m_tasks != nullptr) {
        Tasks::TaskDesc desc;
        desc.title = std::move(title);
        desc.category = "Import";
        pending->token = m_tasks->Begin(std::move(desc));
    }
    Jobs::JobDesc desc;
    desc.name = "Import: read source";
    desc.priority = Jobs::Priority::High;
    m_jobs->Schedule(std::move(desc), [pending, work = std::move(work)] {
        try {
            pending->session = work(&pending->error);
        } catch (const std::exception& e) {
            pending->session.reset();
            pending->error = e.what();
        }
        pending->done = true;
    });
    m_translate = std::move(pending);
}

void EditorScreen::PollTranslate() {
    if (!m_translate || !m_translate->done) return;
    std::shared_ptr<PendingTranslate> pending = std::move(m_translate);
    if (!pending->session) {
        const std::string message = pending->error.empty() ? "Could not read the selected files" : pending->error;
        if (pending->token.IsValid()) pending->token.Fail(message);
        LUMA_USER_ERROR("Import", "{}", message);
        return;
    }
    if (pending->token.IsValid()) pending->token.Complete();
    if (!m_project) return;  // project closed while reading
    FinishOpenImport(std::move(pending->session), pending->folder, pending->reimport);
}

void EditorScreen::OpenImport(const std::vector<std::filesystem::path>& files,
                              const std::filesystem::path& folder, bool reimport) {
    if (!m_project || files.empty()) return;
    std::string title = files.size() == 1 ? "Reading " + files.front().filename().string()
                                          : "Reading " + std::to_string(files.size()) + " files";
    BeginTranslate(std::move(title), folder, reimport, [files](std::string* err) {
        auto session = std::make_unique<Import::ImportSession>();
        if (!session->Translate(files, err)) return std::unique_ptr<Import::ImportSession>{};
        return session;
    });
}

void EditorScreen::FinishOpenImport(std::unique_ptr<Import::ImportSession> session,
                                    const std::filesystem::path& folder, bool reimport) {
    m_importDest = folder.empty() ? m_project->AssetsDir() : folder;
    std::error_code io;
    std::filesystem::create_directories(m_importDest, io);
    if (auto virtualPath = m_assetPaths.ToVirtual(m_importDest)) m_importVirtual = *virtualPath;
    else m_importVirtual = "/Game";
    // A reimport keeps the settings recorded in the .meta (presets would
    // overwrite them) and replaces the existing assets.
    const SerialValue restored = reimport ? session->Settings().ToJson() : SerialValue{};
    m_importSession = std::move(session);
    m_importDialog.Open(m_importSession.get(), m_importDest, m_importVirtual, reimport);
    m_importDialog.LoadPresets(m_project->ConfigDir());
    if (reimport) {
        m_importSession->Settings().FromJson(restored);
        m_importSession->Settings().Common().Set("conflict", std::string("Overwrite"));
    }
}

void EditorScreen::ReimportAsset(const std::filesystem::path& asset) {
    if (!m_project) return;
    BeginTranslate("Reading source of " + asset.filename().string(), asset.parent_path(), true,
                   [asset](std::string* err) { return Import::ImportSession::ForReimport(asset, err); });
}

void EditorScreen::OnImportDialog(Editor::Panels::ImportDialog::Action action) {
    if (!m_importSession || action == Editor::Panels::ImportDialog::Action::None) return;
    if (action == Editor::Panels::ImportDialog::Action::Cancel) {
        m_importSession.reset();
        m_importActive = false;
        return;
    }
    Import::FactoryContext context{m_importSession->Scene(), m_importSession->Settings(), m_materials.get(),
                                   m_assetPaths, {}};
    const Import::ImportPlan plan = m_importSession->Plan(m_importDest);
    m_importSession->Start(plan, context);
    m_importActive = true;
    // Progress and Cancel live in the Background Tasks indicator / panel,
    // like every other long-running editor job.
    if (m_tasks != nullptr) {
        Tasks::TaskDesc desc;
        desc.title = "Importing into " + m_importVirtual;
        desc.unit = "assets";
        desc.total = plan.requests.size();
        desc.cancellable = true;
        desc.category = "Import";
        m_importTotal = plan.requests.size();
        m_importTask = m_tasks->Begin(std::move(desc));
    }
}

void EditorScreen::PollImport() {
    if (!m_importSession || !m_importActive) return;
    if (m_importSession->Running()) {
        if (m_importTask.IsValid()) {
            if (m_importTask.CancelRequested()) m_importSession->Cancel();
            // "37 / 120 assets" in the indicator, same as other counted tasks.
            const f64 total = static_cast<f64>(m_importTask.IsValid() ? m_importTotal : 0);
            m_importTask.SetDone(static_cast<u64>(std::lround(total * m_importSession->Progress())));
        }
        return;
    }
    const auto result = m_importSession->TakeResult();
    m_importActive = false;
    if (m_importTask.IsValid()) {
        // Complete() reports Cancelled when Cancel was pressed in the panel.
        if (result && result->failed > 0 && !m_importTask.CancelRequested()) {
            m_importTask.Fail(std::to_string(result->failed) + " assets failed (see the Console)");
        } else {
            m_importTask.Complete();
        }
        m_importTask = Tasks::TaskToken{};
    }
    if (!result) {
        m_importSession.reset();
        return;
    }
    m_assetRegistry.Scan();
    if (m_sourceWatch) m_sourceWatch->InvalidateAll();
    for (const std::filesystem::path& file : result->files) {
        const AssetData* data = m_assetRegistry.LookupByPath(file);
        const AssetId id = data != nullptr ? data->id : MakeAssetIdFromKey(file.string());
        ThumbnailManager::Get().DirtyThumbnail(id);
        m_contentBrowser.InvalidateThumbnail(id);
    }
    const u32 imported = result->created + result->overwritten;
    LUMA_USER_INFO("Import", "Imported {} assets into {} ({} skipped, {} failed, {} warnings)", imported,
                   m_importVirtual, result->skipped, result->failed, result->warnings.size());
    for (const std::string& message : result->warnings) LUMA_USER_WARN("Import", "{}", message);
    for (const std::string& message : result->messages) LUMA_USER_ERROR("Import", "{}", message);
    m_importSession.reset();
}

bool EditorScreen::SettingsOpen(SettingsKind kind) const {
    return kind == SettingsKind::Project ? m_projectSettings.IsOpen() : m_preferencesOpen;
}

void EditorScreen::CloseSettings(SettingsKind kind) {
    if (kind == SettingsKind::Project) m_projectSettings.Close();
    else {
        m_preferences.SaveChanges();
        m_preferencesOpen = false;
    }
}

void EditorScreen::DrawSettings(SettingsKind kind, Slate::Context& ui, f32 width, f32 height) {
    if (kind == SettingsKind::Project) m_projectSettings.Draw(ui, width, height);
    else {
        m_preferencesOpen = m_preferences.Draw(ui, width, height);
        if (!ui.isMouseDown(0) || !m_preferencesOpen) m_preferences.SaveChanges();
    }
}

void EditorScreen::Draw(Slate::Context& ui, f32 width, f32 height) {
    ui.drawList().SetAntiAliased(true);       // smooth (anti-aliased) edges across the editor UI
    m_uiFontPath = ui.theme().type.uiRegular;  // the material editors load extra sizes of it
    PollTranslate();
    PollImport();
    if (m_sourceWatch) m_sourceWatch->Update();
    Slate::Theme& t = ui.theme();
    if (!m_layoutBuilt) {
        m_panelHost->BuildLayout();
        m_layoutBuilt = true;
    }
    ui.Panel({0, 0, width, height}, t.windowBg);

    Rect menu{0, 0, width, 32};
    ui.GradientRect(menu, t.surface2, t.surface1);
    ui.Panel({0, 31, width, 1}, t.separator);

    ui.LogoMark({20, 16}, 12.0f);
    f32 mx = 42.0f;

    // Main menu bar (Unreal-style): every registered menu with entries, in
    // order; each title drops a Slate menu built from the menu registry.
    std::vector<const Editor::MenuDesc*> barMenus;
    for (const Editor::MenuDesc* m : Editor::Menus().Menus()) {
        if (Editor::Menus().HasEntries(m->id)) barMenus.push_back(m);
    }
    const int menuCount = static_cast<int>(barMenus.size());
    std::vector<u64> ids(barMenus.size());
    std::vector<Rect> rects(barMenus.size());
    int openIndex = -1;
    for (int i = 0; i < menuCount; ++i) {
        f32 w = ui.uiFont().Measure(barMenus[i]->label).x + 22.0f;
        ids[i] = Slate::Context::ID("menubar." + barMenus[i]->id) ^ 0x4D454E55ull;
        rects[i] = {mx, 4, w, 24};
        if (ui.IsMenuOpen(ids[i])) openIndex = i;
        mx += w + 2.0f;
    }
    // While one bar menu is open, hovering another title switches to it
    // (the bar reads the raw pointer: widgets are input-blocked by the menu).
    if (openIndex >= 0) {
        for (int i = 0; i < menuCount; ++i) {
            if (i != openIndex && rects[i].Contains(ui.rawMouse())) {
                const Rect& r = rects[i];
                ui.OpenMenuBelow(ids[i], {r.x, r.y, r.w, r.h + 2.0f},
                                 Editor::Menus().Build(barMenus[i]->id));
                openIndex = i;
                break;
            }
        }
    }
    for (int i = 0; i < menuCount; ++i) {
        const Rect& r = rects[i];
        if (ui.MenuButton(ids[i], r, barMenus[i]->label)) {
            ui.OpenMenuBelow(ids[i], {r.x, r.y, r.w, r.h + 2.0f},
                             Editor::Menus().Build(barMenus[i]->id));
        }
        if (i == openIndex) {
            ui.drawList().AddRectFilledRounded(r, t.menu.hoverFill, t.radius.sm);
            ui.LabelIn(r, barMenus[i]->label, t.text, Slate::Align::Center);
        }
    }

    std::string projName = m_project ? m_project->Name() : "(no project)";
    f32 nameW = ui.uiFont().Measure(projName).x;
    // [dot][name] pill: 24px lead-in for the dot, 12px trailing padding.
    f32 pillW = nameW + 36.0f;
    Rect pill{width - pillW - 10.0f, 5, pillW, 22};
    ui.PanelRoundedBordered(pill, t.surface1, t.outline,
                            t.radius.pill, t.border.hairline);
    ui.PanelRounded({pill.x + 11.0f, pill.y + 7.0f, 7.0f, 7.0f}, t.accent, 3.5f);
    // Heading insets its text by space.lg, so start the rect that much
    // earlier and give it room for the whole name.
    ui.Heading({pill.x + 24.0f - t.space.lg, pill.y, nameW + t.space.lg * 2.0f, pill.h},
               projName, t.text);

    // Window row (asset editor windows dock here; Home = the level editor).
    constexpr f32 kRowH = Editor::AssetWindowManager::kRowH;
    constexpr f32 kStatusBarH = 24.0f;
    const Rect windowRow{0, 32, width, kRowH};
    const f32 top = windowRow.Bottom();  // toolbar starts under the row
    m_windowWorkArea = {0, top, width, height - top - kStatusBarH};
    m_windows.DrawRow(ui, windowRow);
    // A floating window over the editor below it owns the pointer.
    const Slate::Vec2 realMouse = ui.mouse();
    const bool covered = m_windows.CapturesPointer(realMouse);
    if (covered) ui.WarpMouse(-100000.0f, -100000.0f);
    const bool home = !m_windows.DrawDocked(ui, m_windowWorkArea);
    if (home) {
    Rect toolbar{0, top, width, 36};
    ui.GradientRect(toolbar, t.surface1, t.surface0);
    ui.Panel({0, top + 35.0f, width, 1}, t.separator);
    // Left side of the main toolbar: Save (Ctrl+S), like Unreal's toolbar.
    if (m_icons.save) {
        const Rect saveR{10, top + 3.0f, 30, 28};
        if (ui.IconButton(Slate::Context::ID("toolbar.save"), saveR, m_icons.save)) {
            SaveScene();
        }
        ui.Tooltip(Slate::Context::ID("tip.save"), saveR,
                   "Save Scene (Ctrl+S)\nWrite the open scene to disk.");
    }
    const Rect playR{width / 2 - 46, top + 3.0f, 30, 28};
    const Rect pauseR{width / 2 - 14, top + 3.0f, 30, 28};
    const Rect stopR{width / 2 + 18, top + 3.0f, 30, 28};
    if (ui.IconButton(Slate::Context::ID("play"), playR, m_iconPlay)) BeginPlay();
    if (ui.IconButton(Slate::Context::ID("pause"), pauseR, m_iconPause)) m_runner.SetPaused(!m_runner.Paused());
    if (ui.IconButton(Slate::Context::ID("stop"), stopR, m_iconStop)) StopPlay();
    ui.Tooltip(Slate::Context::ID("tip.play"), playR, "Play in Editor");
    ui.Tooltip(Slate::Context::ID("tip.pause"), pauseR, m_runner.Paused() ? "Resume" : "Pause");
    ui.Tooltip(Slate::Context::ID("tip.stop"), stopR, "Stop and restore the authoring scene");
    if (m_runner.PlayScene()) {
        const Rect status{stopR.Right() + 6.0f, stopR.y, 64.0f, stopR.h};
        if (m_runner.Paused()) {
            if (ui.Button(Slate::Context::ID("play.step"), status, "Step")) m_runner.Step();
        } else {
            ui.LabelIn(status, "Playing", t.textDim);
        }
    }

    for (const auto& editor : m_materialEditors) {
        m_dock.SetTitle("mated:" + editor->AssetPath(), editor->Title());
    }
    for (const auto& editor : m_materialEditors) editor->BeginFrame();
    m_dock.Draw(ui, {0, top + 36.0f, width, height - top - 36.0f - kStatusBarH});
    }  // home
    if (covered) ui.WarpMouse(realMouse.x, realMouse.y);
    m_panelHost->Sync();  // follow panel (un)registrations; after the dock applied closes
    // Status bar: background-task indicator on the right (Unreal-style).
    const Rect statusBar{0, height - kStatusBarH, width, kStatusBarH};
    ui.Panel(statusBar, t.surface1);
    ui.Panel({0, statusBar.y, width, 1.0f}, t.separator);
    if (m_tasks) m_backgroundTasks.DrawStatusIndicator(ui, statusBar, *m_tasks);
    // Floating asset editor windows sit above the level editor.
    m_windows.DrawFloating(ui, m_windowWorkArea);
    // Editors closed this frame are freed now (ids: "mated:" / "instance:" / "material:" + path).
    for (const std::string& key : m_editorsToDelete) {
        const auto colon = key.find(':');
        const std::string kind = key.substr(0, colon), path = key.substr(colon + 1);
        auto same = [&](const auto& e) { return e->AssetPath() == path; };
        if (kind == "mated") std::erase_if(m_materialEditors, same);
        if (kind == "material") std::erase_if(m_graphEditors, same);
        if (kind == "instance") std::erase_if(m_instanceEditors, same);
    }
    m_editorsToDelete.clear();
    HandleShortcuts(ui);

    // Content Browser double-click: open the right editor for the activated
    // asset. Materials open the Material Editor (docked lazily on first use).
    {
        AssetId activated = m_contentBrowser.Activated();
        m_contentBrowser.ClearActivated();
        if (const AssetData* data = activated.IsValid() ? m_assetRegistry.Lookup(activated)
                                                        : nullptr) {
            const auto desc = AssetTypes().FindByExtension(data->extension);
            if (desc && desc->open) {
                AssetOpenContext context;
                context.openMaterial = [this](const auto& file) {
                    if (auto path = m_assetPaths.ToVirtual(file)) OpenMaterialEditor(*path);
                };
                desc->open(data->packagePath, context);
            }
        }
    }

    // Floating color pickers — drawn after the dock so the panels aren't
    // clipped to the inspector column (the dock scissor-clips every panel).
    {
        auto ctx = BuildPanelContext();
        m_inspectorPanel.DrawFloatingPickers(ui, ctx);
        for (const auto& editor : m_materialEditors) editor->DrawFloatingPickers(ui);
    }
    if (m_tasks) m_backgroundTasks.DrawPanel(ui, *m_tasks);
    DrawMaterialEditorClosePrompt(ui);
    DrawClosePrompt(ui);
    if (m_importDialog.IsOpen()) {
        OnImportDialog(m_importDialog.Draw(ui, width, height));
    }
}

Entity EditorScreen::PickEntity(const Math::Vec3& origin, const Math::Vec3& dir) const {
    using namespace Math;
    auto& reg = const_cast<Scene&>(ActiveScene()).Registry();
    Entity best = kNullEntity;
    f32 bestT = 1e30f;
    // Slab test of a ray against an AABB; returns the entry parameter.
    auto rayBox = [](const Vec3& o, const Vec3& d, const Vec3& lo, const Vec3& hi, f32& t) {
        f32 tmin = -1e30f, tmax = 1e30f;
        const f32 os[3] = {o.x, o.y, o.z}, ds[3] = {d.x, d.y, d.z};
        const f32 los[3] = {lo.x, lo.y, lo.z}, his[3] = {hi.x, hi.y, hi.z};
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(ds[i]) < 1e-9f) {
                if (os[i] < los[i] || os[i] > his[i]) return false;
                continue;
            }
            f32 t0 = (los[i] - os[i]) / ds[i], t1 = (his[i] - os[i]) / ds[i];
            if (t0 > t1) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmin > tmax) return false;
        }
        if (tmax < 0.0f) return false;
        t = tmin >= 0.0f ? tmin : tmax;
        return true;
    };
    auto transformPoint = [](const Mat4& m, const Vec3& p) {
        return Vec3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
                    m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
                    m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
    };
    auto transformDir = [](const Mat4& m, const Vec3& v) {
        return Vec3(m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
                    m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
                    m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z);
    };

    auto view = reg.view<const TransformComponent>();
    for (Entity e : view) {
        if (reg.all_of<EditorHiddenComponent>(e)) continue;
        const auto& tf = view.get<const TransformComponent>(e);
        f32 t = 0.0f;
        bool hit = false;
        if (const auto* mr = reg.try_get<MeshRendererComponent>(e)) {
            // Local-space bounds, tested against the ray in local space (the
            // unnormalized local direction keeps t in world ray units).
            Vec3 lo{-0.5f, -0.5f, -0.5f}, hi{0.5f, 0.5f, 0.5f};
            if (mr->UsesMeshAsset()) {
                if (const Editor::MeshBounds* bounds = m_bridge.Bounds(mr->mesh)) {
                    lo = bounds->min;
                    hi = bounds->max;
                }
            } else if (mr->primitive == MeshPrimitive::Plane) {
                lo.y = -0.02f;
                hi.y = 0.02f;
            }
            const Mat4 inv = Inverse(tf.Matrix());
            hit = rayBox(transformPoint(inv, origin), transformDir(inv, dir), lo, hi, t);
        } else {
            // Non-mesh actors (lights, cameras, volumes...): a small sphere.
            constexpr f32 kRadius = 0.35f;
            const Vec3 oc = origin - tf.position;
            const f32 b = Dot(oc, dir);
            const f32 c = Dot(oc, oc) - kRadius * kRadius;
            const f32 disc = b * b - c;
            if (disc >= 0.0f) {
                t = -b - std::sqrt(disc);
                hit = t >= 0.0f;
            }
        }
        if (hit && t < bestT) {
            bestT = t;
            best = e;
        }
    }
    return best;
}

std::string EditorScreen::ProjectRelative(const std::filesystem::path& path) const {
    if (!m_project) return path.string();
    std::error_code ec;
    const std::filesystem::path root = m_project->AssetsDir().parent_path();
    std::filesystem::path rel = std::filesystem::relative(path, root, ec);
    if (ec || rel.empty() || rel.native().rfind(L"..", 0) == 0) return path.string();
    return rel.generic_string();
}

void EditorScreen::HandleShortcuts(Slate::Context& ui) {
    // Editor-wide shortcuts. Text entry owns the keyboard while focused, and
    // an open menu hides key presses from this pass entirely.
    if (ui.textFieldFocused() || ui.AnyMenuOpen()) return;
    constexpr int kKeyS = 83, kKeyD = 68, kKeyF = 70, kKeyC = 67, kKeyX = 88, kKeyV = 86;
    const bool ctrl = ui.ctrlDown();
    if (ctrl && ui.keyPressed(kKeyS)) {
        const bool overMaterialEditor =
            std::any_of(m_materialEditors.begin(), m_materialEditors.end(),
                        [&](const auto& e) { return e->Hovered(ui); });
        if (!overMaterialEditor) SaveScene();
        return;
    }
    // Transform tools (Unreal W / E / R). While the right mouse button is
    // held these keys fly the camera instead (handled by the viewport).
    constexpr int kKeyW = 87, kKeyE = 69, kKeyR = 82;
    if (!ctrl && !ui.isMouseDown(1)) {
        if (ui.keyPressed(kKeyW)) m_gizmo.SetMode(GizmoMode::Translate);
        if (ui.keyPressed(kKeyE)) m_gizmo.SetMode(GizmoMode::Rotate);
        if (ui.keyPressed(kKeyR)) m_gizmo.SetMode(GizmoMode::Scale);
    }
    const bool overContent = m_contentBrowser.Hovered();
    if (ctrl && ui.keyPressed(kKeyC) && !overContent && ActiveScene().IsValid(m_selected)) {
        CopyEntities(m_selected);
        return;
    }
    if (ctrl && ui.keyPressed(kKeyX) && !overContent && ActiveScene().IsValid(m_selected)) {
        CutEntities(m_selected);
        return;
    }
    if (ctrl && ui.keyPressed(kKeyV) && !overContent && !m_clipboard.Empty()) {
        PasteEntities(ActiveScene().IsValid(m_selected) ? m_selected : kNullEntity, false);
        return;
    }
    if (!ActiveScene().IsValid(m_selected)) return;
    if (ctrl && ui.keyPressed(kKeyD)) {
        DuplicateEntity(m_selected);
    } else if (!ctrl && ui.keyPressed(kKeyF)) {
        FocusEntity(m_selected);
    } else if (ui.keyDelete() && !overContent) {
        // Over the Content Browser, Delete belongs to assets, not actors.
        DeleteEntity(m_selected);
    }
}

void EditorScreen::SelectOnly(Entity entity) {
    m_selection.clear();
    m_selected = entity;
    if (ActiveScene().IsValid(entity)) m_selection.push_back(entity);
}

std::vector<Entity> EditorScreen::ActionRoots(Entity clicked) const {
    const bool inSelection = std::find(m_selection.begin(), m_selection.end(), clicked) != m_selection.end();
    if (inSelection && m_selection.size() > 1) return SelectionRoots(ActiveScene(), m_selection);
    if (!ActiveScene().IsValid(clicked)) return {};
    return {clicked};
}

bool EditorScreen::IsHidden(Entity entity) const {
    return ActiveScene().IsValid(entity) && ActiveScene().Registry().all_of<EditorHiddenComponent>(entity);
}

void EditorScreen::DuplicateEntity(Entity e) {
    std::vector<Entity> created;
    for (Entity root : ActionRoots(e)) {
        // A one-per-scene holder (e.g. the Environment) stays unique.
        if (const ComponentDesc* single = OnePerSceneComponentOf(ActiveScene(), root)) {
            LUMA_USER_WARN("Scene", "A scene has a single {}; it can't be duplicated",
                           single->displayName);
            continue;
        }
        const std::string base = ActiveScene().Registry().get<NameComponent>(root).name;
        Entity copy = DuplicateSubtree(ActiveScene(), root);
        if (copy == kNullEntity) continue;
        auto renameTree = [&](auto&& self, Entity entity) -> void {
            auto& name = ActiveScene().Registry().get<NameComponent>(entity).name;
            name = UniqueEntityName(ActiveScene(), name);
            for (Entity child : ChildrenOf(ActiveScene(), entity)) self(self, child);
        };
        renameTree(renameTree, copy);
        created.push_back(copy);
        LUMA_USER_INFO("Scene", "Duplicated '{}' as '{}'", base,
                       ActiveScene().Registry().get<NameComponent>(copy).name);
    }
    if (created.empty()) return;
    m_selection = created;
    m_selected = created.back();
}

void EditorScreen::DeleteEntity(Entity e) {
    const std::vector<Entity> roots = ActionRoots(e);
    if (roots.empty()) return;
    m_inspectorPanel.ClosePopups();
    for (Entity root : roots) {
        if (!ActiveScene().IsValid(root)) continue;
        const bool selectedInSubtree = m_selected == root || IsDescendantOf(ActiveScene(), root, m_selected);
        const std::string name = ActiveScene().Registry().get<NameComponent>(root).name;
        DestroySubtree(ActiveScene(), root);
        if (selectedInSubtree) m_selected = kNullEntity;
        LUMA_USER_INFO("Scene", "Deleted '{}'", name);
    }
    std::vector<Entity> still;
    for (Entity entity : m_selection) {
        if (ActiveScene().IsValid(entity)) still.push_back(entity);
    }
    m_selection = std::move(still);
    if (!ActiveScene().IsValid(m_selected)) m_selected = m_selection.empty() ? kNullEntity : m_selection.back();
}

void EditorScreen::FocusEntity(Entity e) {
    if (!ActiveScene().IsValid(e) || !ActiveScene().Registry().all_of<TransformComponent>(e)) return;
    const auto& tf = ActiveScene().Registry().get<TransformComponent>(e);
    m_camTarget = tf.position;
    // Frame by the entity's largest scale axis (unit primitives).
    const f32 extent = std::max({std::fabs(tf.scale.x), std::fabs(tf.scale.y),
                                 std::fabs(tf.scale.z), 1.0f});
    m_camDistance = std::clamp(extent * 4.0f, 3.0f, m_farZ * 0.5f);
    SelectOnly(e);
}

void EditorScreen::CopyEntities(Entity clicked) {
    if (!m_clipboard.Copy(ActiveScene(), ActionRoots(clicked))) {
        LUMA_USER_WARN("Scene", "Nothing to copy");
    }
}

void EditorScreen::CutEntities(Entity clicked) {
    if (!m_clipboard.Cut(ActiveScene(), ActionRoots(clicked))) {
        LUMA_USER_WARN("Scene", "Nothing to cut");
        return;
    }
    LUMA_USER_INFO("Scene", "Cut {} actor(s)", m_clipboard.CutSources().size());
}

void EditorScreen::PasteEntities(Entity clicked, bool asChild) {
    if (m_clipboard.Empty()) return;
    Entity parent = kNullEntity;
    if (asChild) {
        parent = clicked;
    } else if (ActiveScene().IsValid(clicked)) {
        parent = ParentOf(ActiveScene(), clicked);
    }
    if (m_clipboard.IsCut()) {
        for (Entity source : m_clipboard.CutSources()) {
            if (!ActiveScene().IsValid(source)) continue;
            if (parent == source || IsDescendantOf(ActiveScene(), source, parent)) {
                LUMA_USER_WARN("Scene", "Can't paste a cut actor into itself");
                return;
            }
        }
    }
    const std::vector<Entity> ignore = m_clipboard.IsCut() ? m_clipboard.CutSources() : std::vector<Entity>{};
    const std::vector<Entity> created = m_clipboard.Paste(ActiveScene(), parent, ignore);
    if (created.empty()) return;
    if (m_clipboard.IsCut()) {
        for (Entity source : m_clipboard.CutSources()) DestroySubtree(ActiveScene(), source);
        m_clipboard.Clear();
    }
    m_selection = created;
    m_selected = created.back();
    LUMA_USER_INFO("Scene", "Pasted {} actor(s)", created.size());
}

void EditorScreen::CreateEmptyChild(Entity parent) {
    if (!ActiveScene().IsValid(parent)) return;
    Entity child = ActiveScene().CreateEntity(UniqueEntityName(ActiveScene(), "Empty Actor"));
    SetParent(ActiveScene(), child, parent);
    SelectOnly(child);
}

void EditorScreen::SelectChildren(Entity parent) {
    const std::vector<Entity> descendants = DescendantsOf(ActiveScene(), parent);
    if (descendants.empty()) return;
    m_selection = descendants;
    m_selected = descendants.front();
}

void EditorScreen::MoveToRoot(Entity entity) {
    for (Entity root : ActionRoots(entity)) {
        if (ParentOf(ActiveScene(), root) == kNullEntity) continue;
        SetParent(ActiveScene(), root, kNullEntity);
    }
}

void EditorScreen::HideEntities(Entity clicked) {
    for (Entity root : ActionRoots(clicked)) {
        ActiveScene().Registry().emplace_or_replace<EditorHiddenComponent>(root);
        for (Entity child : DescendantsOf(ActiveScene(), root)) {
            ActiveScene().Registry().emplace_or_replace<EditorHiddenComponent>(child);
        }
    }
}

void EditorScreen::ShowAllActors() {
    std::vector<Entity> hidden;
    auto view = ActiveScene().Registry().view<EditorHiddenComponent>();
    for (Entity entity : view) hidden.push_back(entity);
    for (Entity entity : hidden) ActiveScene().Registry().remove<EditorHiddenComponent>(entity);
}

void EditorScreen::CreateSceneAsset(const std::filesystem::path& folder) {
    if (folder.empty()) return;
    std::error_code ec;
    std::filesystem::path path = folder / "NewScene.lscene";
    for (int n = 1; std::filesystem::exists(path, ec); ++n) {
        path = folder / ("NewScene" + std::to_string(n) + ".lscene");
    }
    Scene empty;
    std::string err;
    if (!SceneSerializer::SaveToFile(empty, path, &err)) {
        LUMA_USER_ERROR("Content", "Could not create scene '{}': {}", ProjectRelative(path), err);
        return;
    }
    m_assetRegistry.RefreshPath(path);
    LUMA_USER_INFO("Content", "Created scene '{}'", ProjectRelative(path));
}

void EditorScreen::DeleteAssetPath(const std::filesystem::path& path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return;
#ifdef _WIN32
    // Recycle Bin (recoverable), never a permanent delete. SHFileOperation
    // wants a double-NUL-terminated wide path list.
    std::wstring from = std::filesystem::absolute(path, ec).wstring();
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int rc = SHFileOperationW(&op);
    if (rc != 0 || op.fAnyOperationsAborted) {
        LUMA_USER_ERROR("Content", "Could not delete '{}' (code {})",
                        ProjectRelative(path), rc);
        return;
    }
    LUMA_USER_INFO("Content", "Deleted '{}' (moved to the Recycle Bin)", ProjectRelative(path));
#else
    LUMA_USER_ERROR("Content", "Delete is only supported on Windows (no recycle bin)");
    return;
#endif
    m_assetRegistry.RemoveUnder(path.parent_path());
    m_assetRegistry.RefreshPath(path.parent_path());
    m_assetRegistry.Scan();
}

void EditorScreen::RevealInExplorer(const std::filesystem::path& path) {
#ifdef _WIN32
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(path, ec);
    std::wstring args = std::filesystem::is_directory(abs, ec)
                            ? L"\"" + abs.wstring() + L"\""
                            : L"/select,\"" + abs.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr,
                  SW_SHOWNORMAL);
#else
    (void)path;
#endif
}

}  // namespace Luma
