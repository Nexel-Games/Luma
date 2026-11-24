#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "Luma/Asset/AssetId.h"
#include "Luma/Scene/Prefab.h"
#include "Luma/Scene/Scene.h"

// Editing a prefab asset in place (the asset Inspector's prefab view).
//
// The prefab is instantiated into a private Scene (root at the origin), where
// the regular component drawers edit it. Each Commit captures that scene back
// into the prefab, and when the captured prefab differs from the file it is
// saved (object ids kept, see CapturePrefab). The file is re-read when it
// changes on disk (an Apply from a level instance, an external edit).

namespace Luma::Editor::Panels {

class PrefabAssetEditor {
public:
    // Shows `file` (virtual path `path`). Loads it on the first call, when the
    // path changes and when the file changed on disk since the last load or
    // save. False (with Error()) when it can't be read.
    bool Open(const std::filesystem::path& file, const std::string& path);
    const std::string& Error() const { return m_error; }
    const std::string& Path() const { return m_path; }

    Scene& EditScene() { return *m_scene; }
    Entity Root() const { return m_root; }
    // The object shown below the hierarchy (the root by default).
    Entity Picked() const;
    void Pick(Entity e) { m_picked = e; }
    // A new empty object under `parent`; picked. Removes `e` and its children
    // (never the root).
    Entity AddObject(Entity parent);
    void RemoveObject(Entity e);

    // Captures the edit scene; when it differs from the file, saves it.
    // Returns true when the file was written (the caller syncs the level).
    bool Commit(std::string* error);
    // True when the edit scene differs from the last loaded or saved prefab.
    bool Dirty() const;

    // True (once) after Open reloaded the prefab: popups that point into the
    // old edit scene must close.
    bool TakeReloaded();

private:
    std::string Capture() const;

    std::filesystem::path m_file;
    std::string m_path;
    std::optional<std::filesystem::file_time_type> m_mtime;
    std::unique_ptr<Scene> m_scene;
    PrefabAsset m_prefab;
    std::string m_saved;  // the prefab as last loaded or saved (JSON)
    Entity m_root = kNullEntity;
    Entity m_picked = kNullEntity;
    std::string m_error;
    bool m_reloaded = false;
};

}  // namespace Luma::Editor::Panels
