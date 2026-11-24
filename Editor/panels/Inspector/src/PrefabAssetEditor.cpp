#include "Luma/Editor/Panels/PrefabAssetEditor.h"

#include <system_error>

#include "Luma/Scene/Components.h"
#include "Luma/Scene/Hierarchy.h"

namespace Luma::Editor::Panels {

namespace {

std::optional<std::filesystem::file_time_type> ModifiedTime(const std::filesystem::path& file) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(file, ec);
    if (ec) return std::nullopt;
    return t;
}

}  // namespace

bool PrefabAssetEditor::Open(const std::filesystem::path& file, const std::string& path) {
    const auto mtime = ModifiedTime(file);
    if (m_scene && path == m_path && file == m_file && mtime == m_mtime) return m_error.empty();
    m_file = file;
    m_path = path;
    m_mtime = mtime;
    m_error.clear();
    m_reloaded = true;
    std::string err;
    auto prefab = LoadPrefab(file, &err);
    m_scene = std::make_unique<Scene>();
    m_root = kNullEntity;
    m_picked = kNullEntity;
    if (!prefab) {
        m_error = err.empty() ? std::string("unreadable prefab file") : err;
        m_saved.clear();
        return false;
    }
    m_prefab = std::move(*prefab);
    m_root = InstantiatePrefab(*m_scene, m_prefab, MakeAssetIdFromKey(path), path, {0.0f, 0.0f, 0.0f});
    m_picked = m_root;
    m_saved = SavePrefabToString(m_prefab);
    return true;
}

Entity PrefabAssetEditor::Picked() const {
    if (m_scene && m_scene->IsValid(m_picked)) return m_picked;
    return m_root;
}

Entity PrefabAssetEditor::AddObject(Entity parent) {
    if (!m_scene || !m_scene->IsValid(parent)) return kNullEntity;
    const Entity e = m_scene->CreateEntity("Object");
    // Children start at their parent's location (transforms are world space).
    m_scene->Registry().get<TransformComponent>(e).position =
        m_scene->Registry().get<TransformComponent>(parent).position;
    SetParent(*m_scene, e, parent);
    m_picked = e;
    return e;
}

void PrefabAssetEditor::RemoveObject(Entity e) {
    if (!m_scene || !m_scene->IsValid(e) || e == m_root) return;
    const Entity parent = ParentOf(*m_scene, e);
    DestroySubtree(*m_scene, e);
    m_picked = m_scene->IsValid(parent) ? parent : m_root;
}

std::string PrefabAssetEditor::Capture() const {
    if (!m_scene || !m_scene->IsValid(m_root)) return {};
    return SavePrefabToString(CapturePrefab(*m_scene, m_root, m_prefab.name, &m_prefab));
}

bool PrefabAssetEditor::Dirty() const { return m_scene && m_scene->IsValid(m_root) && Capture() != m_saved; }

bool PrefabAssetEditor::Commit(std::string* error) {
    if (!m_scene || !m_scene->IsValid(m_root)) return false;
    PrefabAsset captured = CapturePrefab(*m_scene, m_root, m_prefab.name, &m_prefab);
    const std::string text = SavePrefabToString(captured);
    if (text == m_saved) return false;
    if (!SavePrefab(captured, m_file, error)) return false;
    m_prefab = std::move(captured);
    m_saved = text;
    m_mtime = ModifiedTime(m_file);  // our own write: no reload
    // Objects added here are now part of the prefab: link them with their ids
    // so the next capture keeps them.
    const AssetId id = MakeAssetIdFromKey(m_path);
    LinkPrefabInstance(*m_scene, m_root, m_prefab, id, m_path);
    return true;
}

bool PrefabAssetEditor::TakeReloaded() {
    const bool r = m_reloaded;
    m_reloaded = false;
    return r;
}

}  // namespace Luma::Editor::Panels
