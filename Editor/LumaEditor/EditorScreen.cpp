#include "EditorScreen.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

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
#include "Luma/Asset/LumaMeshFormat.h"
#include "Luma/Asset/ThumbnailRenderer.h"
#include "Luma/Audio/AudioPreview.h"
#include "Luma/Core/Log.h"
#include "Luma/Scene/Components.h"
#include "Luma/Editor/Panels/MaterialSlots.h"
#include "Luma/Material/MaterialFromTextures.h"
#include "Luma/Scene/MaterialMigration.h"
#include "Luma/Scene/SceneSerializer.h"
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

// Returns `base` unchanged when no entity in the scene shares that name;
// otherwise appends the lowest free number (base 1, base 2, ...) so the first
// of a kind stays unnumbered and duplicates get distinguished.
std::string UniqueEntityName(Luma::Scene& scene, const std::string& base) {
    auto view = scene.Registry().view<const NameComponent>();
    auto taken = [&view](const std::string& name) {
        for (Entity e : view) {
            if (view.get<const NameComponent>(e).name == name) return true;
        }
        return false;
    };
    if (!taken(base)) return base;
    int n = 1;
    while (taken(base + " " + std::to_string(n))) ++n;
    return base + " " + std::to_string(n);
}
}  // namespace

EditorScreen::EditorScreen(const std::filesystem::path& projectFile) {
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

    if (!LoadScene()) {
        CreateEnvironment();
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
    m_preview = std::make_unique<MaterialPreview>(*previewRenderer, *m_materials, m_assetRegistry);
    // Material and mesh thumbnails render on the GPU through the preview
    // scene, so they match the viewport (square captures).
    ThumbnailManager::Get().SetRenderCallback(
        [this](AssetType type, const std::filesystem::path& path, u32 width, u32 height,
               std::vector<u8>& rgba) {
            if (!m_preview || width != height) return false;
            if (type == AssetType::Material) {
                const auto virtualPath = m_assetPaths.ToVirtual(path);
                return virtualPath && m_preview->CaptureMaterial(*virtualPath, width, rgba);
            }
            if (type == AssetType::Mesh) {
                const AssetData* data = m_assetRegistry.LookupByPath(path);
                return data && m_preview->CaptureMesh(data->id, width, rgba);
            }
            return false;
        });
}

void EditorScreen::CreateEnvironment() {
    m_environment = m_scene.CreateEntity("Environment");
    m_scene.Registry().emplace<EnvironmentComponent>(m_environment);
}

bool EditorScreen::LoadScene() {
    if (!m_project) return false;
    std::filesystem::path path = m_project->StartupScenePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;
    std::string err;
    LegacyMaterialList legacy;
    if (!SceneSerializer::LoadFromFile(m_scene, path, &err, &legacy)) {
        LUMA_USER_ERROR("Scene", "Failed to load scene '{}': {}", ProjectRelative(path),
                       err);
        return false;
    }
    m_selected = kNullEntity;
    m_outlinerPanel.ClearSelection();
    m_inspectorPanel.ClosePopups();
    auto envView = m_scene.Registry().view<EnvironmentComponent>();
    if (envView.begin() == envView.end()) CreateEnvironment();
    LUMA_USER_INFO("Scene", "Loaded scene '{}'", ProjectRelative(path));
    // Version-1 scenes carried a color per mesh; move those onto material
    // assets (in memory until the scene is saved).
    if (!legacy.empty()) MigrateLegacyMaterials(m_scene, legacy, *m_materials);
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
    auto view = m_scene.Registry().view<const NameComponent>();
    for (Entity e : view) {
        if (view.get<const NameComponent>(e).name == name) {
            m_selected = e;
            return true;
        }
    }
    return false;
}

std::vector<std::string> EditorScreen::UnsavedAssets() const {
    return m_materials ? m_materials->DirtyAssets() : std::vector<std::string>{};
}

void EditorScreen::RequestClose() {
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
    m_dock.AddPanel(id, raw->Title(),
                    [this, raw](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        raw->Draw(c, r, ctx);
                    },
                    m_icons.tabDetails);
    m_dock.DockWith(id, "viewport", Slate::DockDir::Center, 0.5f);
    m_dock.SetCloseHandler(id, [this, assetPath, raw] {
        if (raw->IsDirty()) {
            m_closingEditor = assetPath;  // ask Save / Don't Save / Cancel
            return false;
        }
        raw->ClosePopups();
        m_editorsToDelete.push_back(assetPath);
        return true;
    });
    LUMA_USER_INFO("Materials", "Opened '{}' in the Material Editor", assetPath);
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
        m_dock.ClosePanel("mated:" + path);
        m_editorsToDelete.push_back(path);
        m_closingEditor.clear();
    };
    if (ui.Button(id ^ 0x5A5Eull, save, "Save")) {
        std::string err;
        if (m_materials->Save(m_closingEditor, &err)) {
            LUMA_USER_INFO("Materials", "Saved '{}'", m_closingEditor);
            close();
        } else {
            LUMA_USER_ERROR("Materials", "Failed to save '{}': {}", m_closingEditor, err);
            m_closingEditor.clear();
        }
        ui.EndModal();
    } else if (ui.Button(id ^ 0xD15Cull, discard, "Don't Save")) {
        // Drop the in-memory edits: reload the asset from disk.
        m_materials->Revert(m_closingEditor);
        close();
        ui.EndModal();
    } else if (ui.Button(id ^ 0xCA9Cull, cancel, "Cancel")) {
        m_closingEditor.clear();
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

void EditorScreen::DropMaterial(const Math::Vec3& origin, const Math::Vec3& dir,
                                const std::string& path) {
    const Entity e = PickEntity(origin, dir);
    auto* mesh = m_scene.IsValid(e) ? m_scene.Registry().try_get<MeshRendererComponent>(e)
                                    : nullptr;
    if (!mesh) {
        LUMA_USER_WARN("Materials", "Drop '{}' onto a mesh actor to assign it", path);
        return;
    }
    Editor::Panels::AssignMaterialSlot(*mesh, 0, path);
    m_selected = e;
    LUMA_USER_INFO("Materials", "Assigned '{}' to '{}'", path,
                   m_scene.Registry().get<NameComponent>(e).name);
}

TextureHandle EditorScreen::AssetThumbnail(const std::string& assetPath) {
    if (!m_renderer) return 0;
    auto file = m_assetPaths.ToFile(assetPath, {"png", "jpg", "jpeg", "tga", "bmp", "psd", "gif",
                                                "lmat", "lmatinst"});
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

void EditorScreen::RefreshMaterialThumbnails() {
    // Material thumbnails depend on their parents and textures, so any
    // material/texture change re-renders all of them.
    for (AssetType type : {AssetType::Material, AssetType::MaterialInstance}) {
        for (const AssetData* a : m_assetRegistry.Filter(type, std::nullopt, {})) {
            ThumbnailManager::Get().DirtyThumbnail(a->id);
            m_contentBrowser.InvalidateThumbnail(a->id);
        }
    }
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
        RefreshMaterialThumbnails();
    } else if (ext == ".lmesh") {
        if (const AssetData* data = m_assetRegistry.LookupByPath(file)) {
            m_bridge.OnMeshFileChanged(data->id);
            if (m_preview) m_preview->OnMeshFileChanged(data->id);
        }
    } else {
        // Changed or removed texture: re-validate (missing -> constant, warned
        // once) and re-upload it in the viewport and the preview.
        m_materials->InvalidateTextures();
        m_bridge.OnTextureFileChanged(*virtualPath);
        if (m_preview) m_preview->OnTextureFileChanged(*virtualPath);
        RefreshMaterialThumbnails();
    }
}

void EditorScreen::SaveScene() {
    if (!m_project) return;
    if (!SaveMaterialAssets()) {
        // The scene may reference unsaved instances (e.g. migrated colors):
        // writing it now would point it at assets that do not exist.
        LUMA_USER_ERROR("Scene", "Scene not saved: fix the material save errors above first");
        return;
    }
    std::filesystem::path path = m_project->StartupScenePath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::string err;
    if (SceneSerializer::SaveToFile(m_scene, path, &err)) {
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

void EditorScreen::CreateActor(Editor::Panels::CreateActorKind kind) {
    int index = m_nextNumber++;

    // --- Primitives / Empty -------------------------------------------------
    if (kind == Editor::Panels::CreateActorKind::Empty) {
        Entity e =
            m_scene.CreateEntity(UniqueEntityName(m_scene, "Empty Actor"));
        m_selected = e;
        return;
    }
    if (kind == Editor::Panels::CreateActorKind::Cube ||
        kind == Editor::Panels::CreateActorKind::Plane ||
        kind == Editor::Panels::CreateActorKind::Sphere ||
        kind == Editor::Panels::CreateActorKind::Cylinder) {
        const char* names[] = {"Cube", "Plane", "Sphere", "Cylinder"};
        const MeshPrimitive prims[] = {MeshPrimitive::Cube, MeshPrimitive::Plane,
                                       MeshPrimitive::Sphere,
                                       MeshPrimitive::Cylinder};
        int i = static_cast<int>(kind) - static_cast<int>(Editor::Panels::CreateActorKind::Cube);
        Entity e =
            m_scene.CreateEntity(UniqueEntityName(m_scene, names[i]));
        auto& mesh = m_scene.Registry().emplace<MeshRendererComponent>(e);
        mesh.primitive = prims[i];  // empty material slots: Default Material
        auto& tf = m_scene.Registry().get<TransformComponent>(e);
        tf.position = Math::Vec3(static_cast<f32>(index - 1) * 2.0f, 1.0f, 0.0f);
        m_selected = e;
        return;
    }

    // --- Lights -------------------------------------------------------------
    if (kind == Editor::Panels::CreateActorKind::DirectionalLight ||
        kind == Editor::Panels::CreateActorKind::PointLight ||
        kind == Editor::Panels::CreateActorKind::SpotLight ||
        kind == Editor::Panels::CreateActorKind::TubeLight) {
        const char* names[] = {"Directional Light", "Point Light",
                               "Spot Light", "Tube Light"};
        const LightType types[] = {LightType::Directional, LightType::Point,
                                   LightType::Spot, LightType::Tube};
        int i = static_cast<int>(kind) - static_cast<int>(Editor::Panels::CreateActorKind::DirectionalLight);
        Entity e =
            m_scene.CreateEntity(UniqueEntityName(m_scene, names[i]));
        auto& light = m_scene.Registry().emplace<LightComponent>(e);
        light.type = types[i];
        auto& tf = m_scene.Registry().get<TransformComponent>(e);
        // Lights sit above the origin. Directional defaults to a three-quarter
        // sun angle (pitch down, yawed) so the ground and meshes both catch
        // light instead of a horizon-grazing identity rotation.
        tf.position = Math::Vec3(static_cast<f32>(index - 1) * 3.0f, 4.0f, 2.0f);
        if (types[i] == LightType::Directional) {
            tf.rotationEuler = Math::Vec3(-50.0f, 35.0f, 0.0f);
        }
        m_selected = e;
        return;
    }

    // --- Environment --------------------------------------------------------
    if (kind == Editor::Panels::CreateActorKind::Environment) {
        auto envView = m_scene.Registry().view<EnvironmentComponent>();
        if (envView.begin() == envView.end()) {
            Entity e = m_scene.CreateEntity("Environment");
            m_scene.Registry().emplace<EnvironmentComponent>(e);
            m_selected = e;
        } else {
            // Only one environment per scene — select the existing one.
            m_selected = *envView.begin();
        }
        return;
    }

    // --- Post Process Volume -------------------------------------------------
    if (kind == Editor::Panels::CreateActorKind::PostProcessVolume) {
        Entity e = m_scene.CreateEntity(
            UniqueEntityName(m_scene, "Post Process Volume"));
        m_scene.Registry().emplace<PostProcessComponent>(e);
        m_selected = e;
        return;
    }
}

Editor::Panels::PanelContext EditorScreen::BuildPanelContext() {
    Editor::Panels::PanelContext ctx;
    ctx.renderer = m_renderer;
    ctx.assetRegistry = &m_assetRegistry;
    ctx.scene = &m_scene;
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
    ctx.onCreateActor = [this](Editor::Panels::CreateActorKind kind) {
        CreateActor(kind);
    };
    ctx.onSaveScene = [this] { SaveScene(); };
    ctx.onImportAssets = [this](const std::filesystem::path& folder) {
        ImportAssetsInto(folder);
    };
    ctx.onReimport = [this](const std::filesystem::path& asset) { ReimportAsset(asset); };
    ctx.sourceChanged = [this](const std::filesystem::path& asset) {
        return m_sourceWatch != nullptr && m_sourceWatch->IsChanged(asset);
    };
    ctx.importProgress = m_importActive ? &m_importProgress : nullptr;
    ctx.onCancelImport = [this] {
        if (m_importSession) m_importSession->Cancel();
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
    ctx.onPickRay = [this](const Math::Vec3& origin, const Math::Vec3& dir) {
        m_selected = PickEntity(origin, dir);
    };
    ctx.materials = m_materials.get();
    ctx.jobs = m_jobs;
    ctx.tasks = m_tasks;
    ctx.sectionCount = [this](Entity e) -> u32 {
        const auto* mesh = m_scene.Registry().try_get<MeshRendererComponent>(e);
        return mesh ? m_bridge.SectionCount(*mesh) : 1u;
    };
    ctx.renderMaterialPreview = [this](const std::string& path, int shape, f32 yaw, f32 pitch,
                                       u32 width, u32 height, bool sky) -> TextureHandle {
        if (!m_preview) return 0;
        return m_preview->Render(path, static_cast<PreviewShape>(std::clamp(shape, 0, 3)), yaw,
                                 pitch, width, height, sky);
    };
    ctx.assetThumbnail = [this](const std::string& path) { return AssetThumbnail(path); };
    ctx.listTextures = [this] { return ListAssets(false); };
    ctx.listMaterials = [this] { return ListAssets(true); };
    ctx.onOpenAsset = [this](const std::string& path) { OpenMaterialEditor(path); };
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
    ctx.onBrowseToAsset = [this](const std::string& path) {
        if (m_materials->IsInstance(path)) {
            if (auto file = m_assetPaths.ToFile(path, {"lmatinst"})) m_contentBrowser.BrowseTo(*file);
        } else if (auto file = m_assetPaths.ToFile(path, {"lmat", "png", "jpg", "jpeg", "tga",
                                                          "bmp", "psd", "gif"})) {
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
        if (m_scene.IsValid(m_selected) &&
            m_scene.Registry().all_of<TransformComponent>(m_selected)) {
            const Vec3 d = m_scene.Registry().get<TransformComponent>(m_selected).position - eye;
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
        m_bridge.Sync(m_scene, *sceneRenderer, *m_materials, m_assetRegistry);
    }
    m_bridge.BuildInstances(m_scene, m_selected, m_instances);

    // The first directional light is the scene's sun: it drives the sky
    // atmosphere, the sun disk, cascaded shadows and the directional term in
    // every renderer. It is not added to the punctual list (it is rendered as
    // the sun instead). Point/spot lights (and any extra directional lights)
    // go into the punctual list.
    m_lights.clear();
    bool hasSun = false;
    SceneLight sun;
    auto lightView =
        m_scene.Registry().view<TransformComponent, LightComponent>();
    for (Entity e : lightView) {
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
    auto envView = m_scene.Registry().view<const EnvironmentComponent>();
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
        auto ppView = m_scene.Registry().view<const PostProcessComponent>();
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

    if (m_scene.IsValid(m_selected) &&
        m_scene.Registry().all_of<TransformComponent>(m_selected)) {
        const auto& tf = m_scene.Registry().get<TransformComponent>(m_selected);
        GizmoTransform gt{tf.position, tf.rotationEuler, tf.scale};
        const auto& lines = m_gizmo.BuildLines(gt, m_gizmoScale, m_view);
        scene.overlayLines = lines.data();
        scene.overlayLineVertexCount = static_cast<u32>(lines.size());
    }
    return scene;
}

void EditorScreen::BuildDock() {
    m_dock.AddPanel("outliner", "World Outliner",
                    [this](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        m_outlinerPanel.Draw(c, r, ctx);
                    },
                    m_icons.tabOutliner);
    m_dock.AddPanel("viewport", "Viewport",
                    [this](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        m_viewportPanel.Draw(c, r, ctx);
                    },
                    m_icons.tabViewport);
    m_dock.AddPanel("inspector", "Inspector",
                    [this](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        m_inspectorPanel.Draw(c, r, ctx);
                    },
                    m_icons.tabDetails);
    m_dock.AddPanel("console", "Console",
                    [this](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        m_consolePanel.Draw(c, r, ctx);
                    },
                    m_icons.tabOutputLog);
    m_dock.AddPanel("content", "Content Browser",
                    [this](Slate::Context& c, const Rect& r) {
                        auto ctx = BuildPanelContext();
                        m_contentBrowser.Draw(c, r, ctx);
                    },
                    m_icons.tabContent);
    m_dock.DockRoot("viewport");
    m_dock.DockWith("console", "viewport", Slate::DockDir::Down, 0.28f);
    m_dock.DockWith("outliner", "viewport", Slate::DockDir::Left, 0.2f);
    m_dock.DockWith("inspector", "viewport", Slate::DockDir::Right, 0.24f);
    m_dock.DockWith("content", "outliner", Slate::DockDir::Down, 0.45f);
    m_dockBuilt = true;
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
    std::filesystem::path target = m_contentBrowser.CurrentFolder();
    if (target.empty()) target = m_project->AssetsDir();
    Import::TranslatorRegistry registry = Import::TranslatorRegistry::CreateDefault();
    std::vector<std::filesystem::path> supported;
    for (const std::filesystem::path& file : files) CollectImportFiles(file, registry, supported);
    OpenImport(supported, target, false);
}

void EditorScreen::ImportAssetsInto(const std::filesystem::path& folder) {
    if (!m_project) return;
    std::filesystem::path target = folder.empty() ? m_project->AssetsDir() : folder;
    Import::TranslatorRegistry registry = Import::TranslatorRegistry::CreateDefault();
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
    m_importProgress = 0.0f;
}

void EditorScreen::PollImport() {
    if (!m_importSession || !m_importActive) return;
    if (m_importSession->Running()) {
        m_importProgress = m_importSession->Progress();
        return;
    }
    const auto result = m_importSession->TakeResult();
    m_importActive = false;
    m_importProgress = 0.0f;
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

void EditorScreen::Draw(Slate::Context& ui, f32 width, f32 height) {
    PollTranslate();
    PollImport();
    if (m_sourceWatch) m_sourceWatch->Update();
    Slate::Theme& t = ui.theme();
    if (!m_dockBuilt) BuildDock();
    ui.Panel({0, 0, width, height}, t.windowBg);

    Rect menu{0, 0, width, 32};
    ui.GradientRect(menu, t.surface2, t.surface1);
    ui.Panel({0, 31, width, 1}, t.separator);

    if (m_iconLogo) {
        ui.Image(m_iconLogo, {8, 4, 24, 24});
    } else {
        ui.LogoMark({20, 16}, 9.0f);
    }
    f32 mx = 42.0f;

    // Main menu bar (Unreal-style): each title drops a Slate menu.
    const char* items[] = {"File", "Edit", "Assets", "Window"};
    constexpr int kMenuCount = static_cast<int>(std::size(items));
    u64 ids[kMenuCount];
    Rect rects[kMenuCount];
    int openIndex = -1;
    for (int i = 0; i < kMenuCount; ++i) {
        f32 w = ui.uiFont().Measure(items[i]).x + 22.0f;
        ids[i] = Slate::Context::ID(items[i]) ^ 0x4D454E55ull;
        rects[i] = {mx, 4, w, 24};
        if (ui.IsMenuOpen(ids[i])) openIndex = i;
        mx += w + 2.0f;
    }
    // While one bar menu is open, hovering another title switches to it
    // (the bar reads the raw pointer: widgets are input-blocked by the menu).
    if (openIndex >= 0) {
        for (int i = 0; i < kMenuCount; ++i) {
            if (i != openIndex && rects[i].Contains(ui.rawMouse())) {
                const Rect& r = rects[i];
                ui.OpenMenuBelow(ids[i], {r.x, r.y, r.w, r.h + 2.0f}, BuildMainMenu(i));
                openIndex = i;
                break;
            }
        }
    }
    for (int i = 0; i < kMenuCount; ++i) {
        const Rect& r = rects[i];
        if (ui.MenuButton(ids[i], r, items[i])) {
            ui.OpenMenuBelow(ids[i], {r.x, r.y, r.w, r.h + 2.0f}, BuildMainMenu(i));
        }
        if (i == openIndex) {
            ui.drawList().AddRectFilledRounded(r, t.menu.hoverFill, t.radius.sm);
            ui.LabelIn(r, items[i], t.text, Slate::Align::Center);
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

    Rect toolbar{0, 32, width, 36};
    ui.GradientRect(toolbar, t.surface1, t.surface0);
    ui.Panel({0, 67, width, 1}, t.separator);
    // Left side of the main toolbar: Save (Ctrl+S), like Unreal's toolbar.
    if (m_icons.save) {
        const Rect saveR{10, 35, 30, 28};
        if (ui.IconButton(Slate::Context::ID("toolbar.save"), saveR, m_icons.save)) {
            SaveScene();
        }
        ui.Tooltip(Slate::Context::ID("tip.save"), saveR,
                   "Save Scene (Ctrl+S)\nWrite the open scene to disk.");
    }
    const Rect playR{width / 2 - 46, 35, 30, 28};
    const Rect pauseR{width / 2 - 14, 35, 30, 28};
    const Rect stopR{width / 2 + 18, 35, 30, 28};
    ui.IconButton(Slate::Context::ID("play"), playR, m_iconPlay);
    ui.IconButton(Slate::Context::ID("pause"), pauseR, m_iconPause);
    ui.IconButton(Slate::Context::ID("stop"), stopR, m_iconStop);
    ui.Tooltip(Slate::Context::ID("tip.play"), playR,
               "Play\nPlay-in-editor is not implemented yet.");
    ui.Tooltip(Slate::Context::ID("tip.pause"), pauseR,
               "Pause\nPlay-in-editor is not implemented yet.");
    ui.Tooltip(Slate::Context::ID("tip.stop"), stopR,
               "Stop\nPlay-in-editor is not implemented yet.");

    for (const auto& editor : m_materialEditors) {
        m_dock.SetTitle("mated:" + editor->AssetPath(), editor->Title());
    }
    for (const auto& editor : m_materialEditors) editor->BeginFrame();
    constexpr f32 kStatusBarH = 24.0f;
    m_dock.Draw(ui, {0, 68.0f, width, height - 68.0f - kStatusBarH});
    // Status bar: background-task indicator on the right (Unreal-style).
    const Rect statusBar{0, height - kStatusBarH, width, kStatusBarH};
    ui.Panel(statusBar, t.surface1);
    ui.Panel({0, statusBar.y, width, 1.0f}, t.separator);
    if (m_tasks) m_backgroundTasks.DrawStatusIndicator(ui, statusBar, *m_tasks);
    // Tabs closed during the dock draw are freed now (the dock dropped them).
    for (const std::string& path : m_editorsToDelete) {
        std::erase_if(m_materialEditors,
                      [&](const auto& e) { return e->AssetPath() == path; });
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
            if (data->type == AssetType::Material || data->type == AssetType::MaterialInstance) {
                if (auto path = m_assetPaths.ToVirtual(data->packagePath)) OpenMaterialEditor(*path);
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
    auto& reg = const_cast<Scene&>(m_scene).Registry();
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
        const auto& tf = view.get<const TransformComponent>(e);
        f32 t = 0.0f;
        bool hit = false;
        if (const auto* mr = reg.try_get<MeshRendererComponent>(e)) {
            // Local-space bounds, tested against the ray in local space (the
            // unnormalized local direction keeps t in world ray units).
            Vec3 lo{-0.5f, -0.5f, -0.5f}, hi{0.5f, 0.5f, 0.5f};
            if (mr->UsesMeshAsset()) {
                if (const Editor::MeshBounds* bounds = m_bridge.Bounds(mr->meshAsset)) {
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
    constexpr int kKeyS = 83, kKeyD = 68, kKeyF = 70;
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
    if (!m_scene.IsValid(m_selected)) return;
    if (ctrl && ui.keyPressed(kKeyD)) {
        DuplicateEntity(m_selected);
    } else if (!ctrl && ui.keyPressed(kKeyF)) {
        FocusEntity(m_selected);
    } else if (ui.keyDelete() && !m_contentBrowser.Hovered()) {
        // Over the Content Browser, Delete belongs to assets, not actors.
        DeleteEntity(m_selected);
    }
}

void EditorScreen::DuplicateEntity(Entity e) {
    if (!m_scene.IsValid(e)) return;
    if (m_scene.Registry().all_of<EnvironmentComponent>(e)) return;  // one per scene
    const std::string base = m_scene.Registry().get<NameComponent>(e).name;
    Entity copy = m_scene.DuplicateEntity(e);
    if (copy == kNullEntity) return;
    const std::string name = UniqueEntityName(m_scene, base);
    m_scene.Registry().get<NameComponent>(copy).name = name;
    m_selected = copy;
    LUMA_USER_INFO("Scene", "Duplicated '{}' as '{}'", base, name);
}

void EditorScreen::DeleteEntity(Entity e) {
    if (!m_scene.IsValid(e)) return;
    if (m_selected == e) {
        m_inspectorPanel.ClosePopups();
        m_selected = kNullEntity;
    }
    const std::string name = m_scene.Registry().get<NameComponent>(e).name;
    m_scene.DestroyEntity(e);
    LUMA_USER_INFO("Scene", "Deleted '{}'", name);
}

void EditorScreen::FocusEntity(Entity e) {
    if (!m_scene.IsValid(e)) return;
    const auto& tf = m_scene.Registry().get<TransformComponent>(e);
    m_camTarget = tf.position;
    // Frame by the entity's largest scale axis (unit primitives).
    const f32 extent = std::max({std::fabs(tf.scale.x), std::fabs(tf.scale.y),
                                 std::fabs(tf.scale.z), 1.0f});
    m_camDistance = std::clamp(extent * 4.0f, 3.0f, m_farZ * 0.5f);
    m_selected = e;
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

std::vector<Slate::MenuEntry> EditorScreen::BuildMainMenu(int index) {
    using Slate::Icon;
    Slate::MenuBuilder b;
    const bool hasSel = m_scene.IsValid(m_selected);
    auto when = [](bool enabled, const char* tip = "") {
        Slate::MenuEntryOptions o;
        o.enabled = enabled;
        o.tooltip = tip;
        return o;
    };
    switch (index) {
        case 0:  // File
            b.BeginSection("Scene");
            {
                auto o = when(m_project.has_value(), "Write the open scene to disk.");
                o.shortcut = "Ctrl+S";
                b.AddEntry("Save Scene", Icon::Save, [this] { SaveScene(); }, o);
            }
            b.BeginSection("Project");
            b.AddEntry("Show Project in Explorer", Icon::FolderOpen,
                       [this] {
                           if (m_project) RevealInExplorer(m_project->AssetsDir().parent_path());
                       },
                       when(m_project.has_value()));
            break;
        case 1:  // Edit
            b.BeginSection("Selection");
            {
                auto o = when(hasSel && !m_scene.Registry().all_of<EnvironmentComponent>(m_selected));
                o.shortcut = "Ctrl+D";
                b.AddEntry("Duplicate", Icon::Plus, [this] { DuplicateEntity(m_selected); }, o);
            }
            {
                auto o = when(hasSel);
                o.shortcut = "Delete";
                b.AddEntry("Delete", Icon::Trash, [this] { DeleteEntity(m_selected); }, o);
            }
            {
                auto o = when(hasSel);
                o.shortcut = "F";
                b.AddEntry("Focus in Viewport", Icon::Eye, [this] { FocusEntity(m_selected); }, o);
            }
            break;
        case 2:  // Assets
            b.BeginSection("Content");
            b.AddEntry("Import...", Icon::Plus,
                       [this] { ImportAssetsInto(m_contentBrowser.CurrentFolder()); },
                       when(m_project.has_value(), "Import files into the open Content Browser folder."));
            b.AddEntry("Refresh Content", Icon::Refresh, [this] { m_assetRegistry.Scan(); },
                       when(true, "Rescan the project's content from disk."));
            break;
        case 3:  // Window
            b.BeginSection("Layout");
            b.AddEntry("Reset Layout", Icon::Grip,
                       [this] {
                           m_dock = Slate::DockSpace{};
                           m_dockBuilt = false;
                       },
                       when(true, "Restore the default panel arrangement."));
            break;
        default:
            break;
    }
    return b.Build();
}

}  // namespace Luma
