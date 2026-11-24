#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Asset/AssetId.h"
#include "Luma/Core/Registration.h"
#include "Luma/Math/Math.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Serialization/SerialValue.h"

// Prefabs: a reusable actor subtree saved as an asset (.lprefab), placed as
// linked instances (Unity's model, per-property overrides).
//
// Every entity of an instance carries a PrefabLinkComponent naming the prefab
// object it mirrors and that object's components at the last sync (its
// baseline). An instance's field is overridden exactly when it differs from
// the baseline; nothing has to report edits. When the prefab changes, each
// field that still equals its baseline takes the new value, overridden fields
// keep theirs, and the baseline becomes the new prefab.
//
// Rules:
//   - the instance root's Transform is the instance's own placement (never an
//     override, never synced);
//   - other objects' Transform positions are stored relative to the prefab
//     root (Luma's hierarchy is organizational: transforms are world space);
//   - components the user removed, and objects the user deleted, stay gone;
//   - components and objects added to or removed from the prefab follow.
//
// Instance entities keep their full component data, so a missing prefab
// costs nothing: the instance keeps its last values.

namespace Luma {

class ComponentRegistry;

inline constexpr const char* kPrefabLinkComponentId = "PrefabLink";
inline constexpr const char* kPrefabExtension = ".lprefab";

struct PrefabLinkComponent {
    AssetId prefab{};
    std::string prefabPath;           // virtual path (display; the id is the identity)
    u64 objectId = 0;                 // the prefab object this entity mirrors
    bool root = false;                // the instance root
    SerialValue baseline;             // that object's components at the last sync
    std::vector<u64> knownObjects;    // root only: prefab objects present at the last sync
};

struct PrefabObject {
    u64 id = 0;
    u64 parent = 0;          // 0 = the prefab root
    SerialValue components;  // component id -> payload (Transform root-relative)
    SerialValue owners;      // component id -> owning plugin (non-engine components)
};

struct PrefabAsset {
    std::string name;
    std::vector<PrefabObject> objects;  // objects[0] is the root; parents before children
    // Object ids are never reused: a removed object's id would otherwise make
    // instances that have not seen the removal mistake a new object for it.
    u64 nextObjectId = 1;
    const PrefabObject* Find(u64 id) const;
};

// The id the prefab's next new object gets (past every id it has ever used).
u64 NextPrefabObjectId(const PrefabAsset& prefab);

struct PrefabOverride {
    enum class Kind : u8 { Field, AddedComponent, RemovedComponent };
    std::string component;
    std::string field;  // empty for component-level overrides
    Kind kind = Kind::Field;
};

// --- Files -----------------------------------------------------------------------
std::string SavePrefabToString(const PrefabAsset& prefab, bool pretty = true);
std::optional<PrefabAsset> LoadPrefabFromString(std::string_view json, std::string* error);
bool SavePrefab(const PrefabAsset& prefab, const std::filesystem::path& file, std::string* error);
std::optional<PrefabAsset> LoadPrefab(const std::filesystem::path& file, std::string* error);

// --- Authoring -----------------------------------------------------------------
// The prefab of `root` and its descendants, in hierarchy order. A fresh actor
// gets ids 1..n. When `root` is an instance root, its linked objects keep
// their ids and new objects get ids past every id the prefab has used
// (`previous`, the prefab as it was, raises that floor further).
PrefabAsset CapturePrefab(const Scene& scene, Entity root, std::string name,
                          const PrefabAsset* previous = nullptr);
// Links `root`'s subtree (as captured) to the prefab: it becomes an instance.
void LinkPrefabInstance(Scene& scene, Entity root, const PrefabAsset& prefab, const AssetId& id,
                        const std::string& path);
// A new linked instance whose root sits at `position`.
Entity InstantiatePrefab(Scene& scene, const PrefabAsset& prefab, const AssetId& id, const std::string& path,
                         const Math::Vec3& position);

// --- Instances -------------------------------------------------------------------
// Brings every instance of prefab `id` up to date. Returns how many instances changed.
usize SyncPrefabInstances(Scene& scene, const AssetId& id, const PrefabAsset& prefab);
// The instance root `entity` belongs to (kNullEntity when not part of one).
Entity PrefabInstanceRoot(const Scene& scene, Entity entity);
// The prefab ids of every instance in the scene (each once).
std::vector<AssetId> PrefabsInScene(const Scene& scene);
// What `entity` overrides of its prefab object.
std::vector<PrefabOverride> PrefabOverrides(const Scene& scene, Entity entity);
// True when any entity of the instance rooted at `root` overrides something.
bool PrefabInstanceModified(const Scene& scene, Entity root);
// Writes the instance's current values into `prefab` (every object; objects
// the user added under it join the prefab and get linked; objects the user
// deleted leave it). The root's placement stays the prefab's. The caller
// saves the file and syncs the other instances.
void ApplyPrefabInstance(Scene& scene, Entity root, PrefabAsset& prefab);
// Makes the instance match `prefab` again: overridden values, removed
// components and deleted objects come back, added components go. The root's
// placement stays.
void RevertPrefabInstance(Scene& scene, Entity root, const PrefabAsset& prefab);
// Breaks the link: the entities keep their current values.
void UnpackPrefabInstance(Scene& scene, Entity root);
// `copy` was duplicated or pasted from linked objects. A whole copy (its root
// is an instance root) stays a linked copy; a copied part of one becomes plain
// objects (two entities never mirror the same prefab object).
void DetachDuplicatedPrefabObjects(Scene& scene, Entity copy);
// A prefab was renamed or moved: entities linked to `oldPath` now link to
// `newId` / `newPath`. Returns how many entities changed.
usize RetargetPrefabLinks(Scene& scene, std::string_view oldPath, const AssetId& newId, const std::string& newPath);

// Registers PrefabLinkComponent (serialized, hidden from Add Component).
RegistrationToken RegisterPrefabLinkComponent(ComponentRegistry& registry);

}  // namespace Luma
