// Prefabs: capture an actor subtree, link instances to it, propagate prefab
// changes to every field an instance hasn't overridden, Apply / Revert /
// Unpack, and the .lprefab and scene round trips.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "Luma/Scene/Components.h"
#include "Luma/Scene/Hierarchy.h"
#include "Luma/Scene/Prefab.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SceneSerializer.h"

using Catch::Approx;
using namespace Luma;

namespace {

const AssetId kLampId = MakeAssetIdFromKey("/Game/Prefabs/Lamp");
const std::string kLampPath = "/Game/Prefabs/Lamp";

// Lamp: root "Lamp" at (10, 0, 0) with a point light child "Bulb" 2 m above it.
struct LampScene {
    Scene scene;
    Entity lamp = kNullEntity;
    Entity bulb = kNullEntity;
    LampScene() {
        lamp = scene.CreateEntity("Lamp");
        scene.Registry().get<TransformComponent>(lamp).position = {10, 0, 0};
        bulb = scene.CreateEntity("Bulb");
        scene.Registry().get<TransformComponent>(bulb).position = {10, 2, 0};
        auto& light = scene.Registry().emplace<LightComponent>(bulb);
        light.intensity = 5.0f;
        REQUIRE(SetParent(scene, bulb, lamp));
    }
};

f32 Intensity(const Scene& s, Entity e) { return s.Registry().get<LightComponent>(e).intensity; }

Entity ChildNamed(const Scene& s, Entity root, const std::string& name) {
    for (Entity c : ChildrenOf(s, root)) {
        if (s.Registry().get<NameComponent>(c).name == name) return c;
    }
    return kNullEntity;
}

}  // namespace

TEST_CASE("Capturing a prefab links the actor it came from", "[prefab]") {
    LampScene l;
    const PrefabAsset asset = CapturePrefab(l.scene, l.lamp, "Lamp");
    REQUIRE(asset.objects.size() == 2);
    CHECK(asset.objects[0].parent == 0);  // root first
    CHECK(asset.objects[1].parent == asset.objects[0].id);
    LinkPrefabInstance(l.scene, l.lamp, asset, kLampId, kLampPath);
    CHECK(PrefabInstanceRoot(l.scene, l.lamp) == l.lamp);
    CHECK(PrefabInstanceRoot(l.scene, l.bulb) == l.lamp);
    // Right after linking nothing differs from the prefab.
    CHECK(PrefabOverrides(l.scene, l.lamp).empty());
    CHECK(PrefabOverrides(l.scene, l.bulb).empty());
}

TEST_CASE("Instances are placed where they are dropped, children keep their offsets", "[prefab]") {
    LampScene l;
    const PrefabAsset asset = CapturePrefab(l.scene, l.lamp, "Lamp");
    const Entity copy = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {0, 0, -5});
    REQUIRE(l.scene.IsValid(copy));
    CHECK(l.scene.Registry().get<TransformComponent>(copy).position.z == Approx(-5.0f));
    const Entity bulb = ChildNamed(l.scene, copy, "Bulb");
    REQUIRE(l.scene.IsValid(bulb));
    const Math::Vec3 p = l.scene.Registry().get<TransformComponent>(bulb).position;
    CHECK(p.x == Approx(0.0f));
    CHECK(p.y == Approx(2.0f));
    CHECK(p.z == Approx(-5.0f));
    CHECK(Intensity(l.scene, bulb) == Approx(5.0f));
    CHECK(PrefabOverrides(l.scene, bulb).empty());
}

TEST_CASE("Prefab changes reach every field an instance hasn't overridden", "[prefab]") {
    LampScene l;
    PrefabAsset asset = CapturePrefab(l.scene, l.lamp, "Lamp");
    const Entity a = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {0, 0, 0});
    const Entity b = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {5, 0, 0});
    const Entity bulbA = ChildNamed(l.scene, a, "Bulb");
    const Entity bulbB = ChildNamed(l.scene, b, "Bulb");
    // Instance B overrides its intensity.
    l.scene.Registry().get<LightComponent>(bulbB).intensity = 50.0f;
    const auto overrides = PrefabOverrides(l.scene, bulbB);
    REQUIRE(overrides.size() == 1);
    CHECK(overrides[0].component == "Light");
    CHECK(overrides[0].field == "intensity");

    // The prefab's light gets brighter and changes colour.
    Scene edit;
    const Entity root = InstantiatePrefab(edit, asset, kLampId, kLampPath, {0, 0, 0});
    auto& light = edit.Registry().get<LightComponent>(ChildNamed(edit, root, "Bulb"));
    light.intensity = 9.0f;
    light.color = {1.0f, 0.5f, 0.0f};
    asset = CapturePrefab(edit, root, "Lamp");
    SyncPrefabInstances(l.scene, kLampId, asset);

    CHECK(Intensity(l.scene, bulbA) == Approx(9.0f));  // followed the prefab
    CHECK(Intensity(l.scene, bulbB) == Approx(50.0f)); // kept its override
    CHECK(l.scene.Registry().get<LightComponent>(bulbB).color.y == Approx(0.5f));  // non-overridden field followed
    // Placement stays the instance's own.
    CHECK(l.scene.Registry().get<TransformComponent>(b).position.x == Approx(5.0f));
}

TEST_CASE("An instance root's location is its own, not an override", "[prefab]") {
    LampScene l;
    const PrefabAsset asset = CapturePrefab(l.scene, l.lamp, "Lamp");
    const Entity copy = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {0, 0, 0});
    l.scene.Registry().get<TransformComponent>(copy).position = {3, 4, 5};
    CHECK(PrefabOverrides(l.scene, copy).empty());
    SyncPrefabInstances(l.scene, kLampId, asset);
    CHECK(l.scene.Registry().get<TransformComponent>(copy).position.y == Approx(4.0f));
}

TEST_CASE("Components and objects added to or removed from a prefab follow, user removals stick", "[prefab]") {
    LampScene l;
    PrefabAsset asset = CapturePrefab(l.scene, l.lamp, "Lamp");
    const Entity a = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {0, 0, 0});
    const Entity b = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {5, 0, 0});
    // Instance B: the user deletes its bulb.
    DestroySubtree(l.scene, ChildNamed(l.scene, b, "Bulb"));

    // The prefab gains a mesh on the root and a second child.
    Scene edit;
    const Entity root = InstantiatePrefab(edit, asset, kLampId, kLampPath, {0, 0, 0});
    edit.Registry().emplace<MeshRendererComponent>(root);
    const Entity shade = edit.CreateEntity("Shade");
    REQUIRE(SetParent(edit, shade, root));
    asset = CapturePrefab(edit, root, "Lamp");
    SyncPrefabInstances(l.scene, kLampId, asset);

    CHECK(l.scene.Registry().all_of<MeshRendererComponent>(a));
    CHECK(l.scene.IsValid(ChildNamed(l.scene, a, "Shade")));
    CHECK(l.scene.IsValid(ChildNamed(l.scene, b, "Shade")));
    CHECK_FALSE(l.scene.IsValid(ChildNamed(l.scene, b, "Bulb")));  // the user's deletion sticks

    // The prefab drops its shade again: instances lose it too.
    Scene edit2;
    const Entity root2 = InstantiatePrefab(edit2, asset, kLampId, kLampPath, {0, 0, 0});
    DestroySubtree(edit2, ChildNamed(edit2, root2, "Shade"));
    asset = CapturePrefab(edit2, root2, "Lamp");
    SyncPrefabInstances(l.scene, kLampId, asset);
    CHECK_FALSE(l.scene.IsValid(ChildNamed(l.scene, a, "Shade")));
}

TEST_CASE("Re-capturing an edited prefab keeps the identity of its objects", "[prefab]") {
    LampScene l;
    const Entity shade = l.scene.CreateEntity("Shade");
    l.scene.Registry().emplace<LightComponent>(shade).intensity = 1.0f;
    REQUIRE(SetParent(l.scene, shade, l.lamp));
    PrefabAsset asset = CapturePrefab(l.scene, l.lamp, "Lamp");
    const Entity a = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {0, 0, 0});
    l.scene.Registry().get<LightComponent>(ChildNamed(l.scene, a, "Shade")).intensity = 42.0f;  // override

    // The prefab loses its bulb (the object before the shade).
    Scene edit;
    const Entity root = InstantiatePrefab(edit, asset, kLampId, kLampPath, {0, 0, 0});
    DestroySubtree(edit, ChildNamed(edit, root, "Bulb"));
    asset = CapturePrefab(edit, root, "Lamp");
    SyncPrefabInstances(l.scene, kLampId, asset);

    CHECK_FALSE(l.scene.IsValid(ChildNamed(l.scene, a, "Bulb")));
    const Entity kept = ChildNamed(l.scene, a, "Shade");
    REQUIRE(l.scene.IsValid(kept));
    CHECK(Intensity(l.scene, kept) == Approx(42.0f));  // still the user's shade, override intact

    // An object added afterwards never reuses the removed bulb's id.
    const Entity cap = edit.CreateEntity("Cap");
    REQUIRE(SetParent(edit, cap, root));
    asset = CapturePrefab(edit, root, "Lamp");
    std::vector<u64> ids;
    for (const PrefabObject& o : asset.objects) ids.push_back(o.id);
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    CHECK(ids.back() == 4);  // 1 lamp, 2 bulb (gone), 3 shade, 4 cap
}

TEST_CASE("Apply pushes an instance's overrides into the prefab, Revert drops them", "[prefab]") {
    LampScene l;
    PrefabAsset asset = CapturePrefab(l.scene, l.lamp, "Lamp");
    const Entity a = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {0, 0, 0});
    const Entity b = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {5, 0, 0});
    l.scene.Registry().get<LightComponent>(ChildNamed(l.scene, a, "Bulb")).intensity = 20.0f;

    ApplyPrefabInstance(l.scene, a, asset);
    SyncPrefabInstances(l.scene, kLampId, asset);
    CHECK(Intensity(l.scene, ChildNamed(l.scene, b, "Bulb")) == Approx(20.0f));
    CHECK(PrefabOverrides(l.scene, ChildNamed(l.scene, a, "Bulb")).empty());

    l.scene.Registry().get<LightComponent>(ChildNamed(l.scene, b, "Bulb")).intensity = 1.0f;
    DestroySubtree(l.scene, ChildNamed(l.scene, a, "Bulb"));
    RevertPrefabInstance(l.scene, b, asset);
    RevertPrefabInstance(l.scene, a, asset);
    CHECK(l.scene.IsValid(ChildNamed(l.scene, a, "Bulb")));  // a deleted object comes back
    CHECK(Intensity(l.scene, ChildNamed(l.scene, b, "Bulb")) == Approx(20.0f));
}

TEST_CASE("Unpacking an instance keeps its values and drops the link", "[prefab]") {
    LampScene l;
    const PrefabAsset asset = CapturePrefab(l.scene, l.lamp, "Lamp");
    const Entity a = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {0, 0, 0});
    const Entity bulb = ChildNamed(l.scene, a, "Bulb");
    UnpackPrefabInstance(l.scene, a);
    CHECK(PrefabInstanceRoot(l.scene, a) == kNullEntity);
    CHECK(PrefabInstanceRoot(l.scene, bulb) == kNullEntity);
    CHECK(Intensity(l.scene, bulb) == Approx(5.0f));
}

TEST_CASE("Prefab files and linked scenes round-trip", "[prefab]") {
    LampScene l;
    const PrefabAsset asset = CapturePrefab(l.scene, l.lamp, "Lamp");
    std::string error;
    const auto reread = LoadPrefabFromString(SavePrefabToString(asset), &error);
    REQUIRE(reread);
    REQUIRE(reread->objects.size() == 2);
    CHECK(reread->name == "Lamp");

    const Entity a = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {0, 0, 0});
    l.scene.Registry().get<LightComponent>(ChildNamed(l.scene, a, "Bulb")).intensity = 7.0f;
    Scene loaded;
    REQUIRE(SceneSerializer::LoadFromString(loaded, SceneSerializer::SaveToString(l.scene), &error));
    Entity root = kNullEntity;
    for (Entity e : loaded.Registry().view<NameComponent>()) {
        if (PrefabInstanceRoot(loaded, e) == e) root = e;
    }
    REQUIRE(loaded.IsValid(root));
    const Entity bulb = ChildNamed(loaded, root, "Bulb");
    REQUIRE(loaded.IsValid(bulb));
    // The override is still an override after the round trip.
    const auto overrides = PrefabOverrides(loaded, bulb);
    REQUIRE(overrides.size() == 1);
    CHECK(overrides[0].field == "intensity");
}

TEST_CASE("Renaming a prefab retargets its instances", "[prefab]") {
    LampScene l;
    const PrefabAsset asset = CapturePrefab(l.scene, l.lamp, "Lamp");
    const Entity a = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {0, 0, 0});
    const AssetId renamedId = MakeAssetIdFromKey("/Game/Props/StreetLamp");
    CHECK(RetargetPrefabLinks(l.scene, kLampPath, renamedId, "/Game/Props/StreetLamp") == 2);
    const auto& link = l.scene.Registry().get<PrefabLinkComponent>(ChildNamed(l.scene, a, "Bulb"));
    CHECK(link.prefabPath == "/Game/Props/StreetLamp");
    CHECK(link.prefab == renamedId);
    CHECK(RetargetPrefabLinks(l.scene, kLampPath, renamedId, "/Game/Props/StreetLamp") == 0);
}

TEST_CASE("A duplicated part of a prefab copy becomes plain objects, a duplicated copy stays linked", "[prefab]") {
    LampScene l;
    const PrefabAsset asset = CapturePrefab(l.scene, l.lamp, "Lamp");
    const Entity a = InstantiatePrefab(l.scene, asset, kLampId, kLampPath, {0, 0, 0});

    const Entity bulbCopy = DuplicateSubtree(l.scene, ChildNamed(l.scene, a, "Bulb"));
    REQUIRE(l.scene.IsValid(bulbCopy));
    l.scene.Registry().get<NameComponent>(bulbCopy).name = "Bulb_1";  // as the editor names duplicates
    DetachDuplicatedPrefabObjects(l.scene, bulbCopy);
    CHECK_FALSE(l.scene.Registry().all_of<PrefabLinkComponent>(bulbCopy));
    CHECK(PrefabInstanceRoot(l.scene, ChildNamed(l.scene, a, "Bulb")) == a);  // the original keeps its link

    const Entity copy = DuplicateSubtree(l.scene, a);
    DetachDuplicatedPrefabObjects(l.scene, copy);
    CHECK(PrefabInstanceRoot(l.scene, copy) == copy);
    CHECK(PrefabInstanceRoot(l.scene, ChildNamed(l.scene, copy, "Bulb")) == copy);
}
