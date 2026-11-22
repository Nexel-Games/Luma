#include "Luma/Scene/ComponentRegistry.h"
#include <catch2/catch_test_macros.hpp>

#include "Luma/Scene/ActorClipboard.h"
#include "Luma/Scene/Hierarchy.h"
#include "Luma/Scene/SceneSerializer.h"

using namespace Luma;

TEST_CASE("Parenting rejects cycles and lists children", "[scene][hierarchy]") {
    Scene scene;
    Entity root = scene.CreateEntity("Root");
    Entity child = scene.CreateEntity("Child");
    Entity grand = scene.CreateEntity("Grand");
    REQUIRE(SetParent(scene, child, root));
    REQUIRE(SetParent(scene, grand, child));
    CHECK(ParentOf(scene, child) == root);
    CHECK(ChildrenOf(scene, root).size() == 1);
    CHECK(DescendantsOf(scene, root).size() == 2);
    CHECK(IsDescendantOf(scene, root, grand));
    CHECK_FALSE(SetParent(scene, root, grand));
    CHECK_FALSE(SetParent(scene, child, child));
    CHECK(ParentOf(scene, root) == kNullEntity);

    const Math::Vec3 position = scene.Registry().get<TransformComponent>(child).position;
    REQUIRE(SetParent(scene, child, kNullEntity));
    CHECK(ParentOf(scene, child) == kNullEntity);
    CHECK(scene.Registry().get<TransformComponent>(child).position.x == position.x);
}

TEST_CASE("Hierarchy survives save and reload", "[scene][hierarchy]") {
    Scene scene;
    Entity root = scene.CreateEntity("Root");
    Entity child = scene.CreateEntity("Child");
    scene.Registry().get<TransformComponent>(child).position = Math::Vec3{4.0f, 5.0f, 6.0f};
    REQUIRE(SetParent(scene, child, root));

    const std::string json = SceneSerializer::SaveToString(scene);
    Scene loaded;
    std::string error;
    REQUIRE(SceneSerializer::LoadFromString(loaded, json, &error));
    Entity loadedRoot = kNullEntity;
    Entity loadedChild = kNullEntity;
    auto view = loaded.Registry().view<const NameComponent>();
    for (Entity entity : view) {
        const std::string& name = view.get<const NameComponent>(entity).name;
        if (name == "Root") loadedRoot = entity;
        if (name == "Child") loadedChild = entity;
    }
    REQUIRE(loaded.IsValid(loadedRoot));
    REQUIRE(loaded.IsValid(loadedChild));
    CHECK(ParentOf(loaded, loadedChild) == loadedRoot);
    CHECK(loaded.Registry().get<TransformComponent>(loadedChild).position.y == 5.0f);
}

TEST_CASE("Duplicate and delete follow the subtree", "[scene][hierarchy]") {
    Scene scene;
    Entity root = scene.CreateEntity("Body");
    Entity arm = scene.CreateEntity("Arm");
    REQUIRE(SetParent(scene, arm, root));
    scene.Registry().emplace<MeshRendererComponent>(root).materials = {"/Game/M_Paint"};
    scene.Registry().emplace<EditorHiddenComponent>(root);

    Entity copy = DuplicateSubtree(scene, root);
    REQUIRE(scene.IsValid(copy));
    CHECK(copy != root);
    CHECK(ParentOf(scene, copy) == kNullEntity);
    CHECK_FALSE(scene.Registry().all_of<EditorHiddenComponent>(copy));
    auto copies = ChildrenOf(scene, copy);
    REQUIRE(copies.size() == 1);
    CHECK(scene.Registry().get<NameComponent>(copies[0]).name == "Arm");
    CHECK(scene.Registry().get<MeshRendererComponent>(copy).materials[0] == "/Game/M_Paint");

    DestroySubtree(scene, root);
    CHECK_FALSE(scene.IsValid(root));
    CHECK_FALSE(scene.IsValid(arm));
    CHECK(scene.IsValid(copy));
}

TEST_CASE("Clipboard paste is independent of the source", "[scene][hierarchy]") {
    Scene scene;
    Entity body = scene.CreateEntity("Body");
    Entity arm = scene.CreateEntity("Arm");
    REQUIRE(SetParent(scene, arm, body));
    scene.Registry().emplace<MeshRendererComponent>(body).mesh = "/Game/SM_Body";
    scene.Registry().get<TransformComponent>(body).position = Math::Vec3{1.0f, 2.0f, 3.0f};

    ActorClipboard clipboard;
    REQUIRE(clipboard.Copy(scene, {body}));
    DestroySubtree(scene, body);
    CHECK_FALSE(scene.IsValid(body));

    Entity host = scene.CreateEntity("Host");
    auto pasted = clipboard.Paste(scene, host, {});
    REQUIRE(pasted.size() == 1);
    CHECK(pasted[0] != body);
    CHECK(ParentOf(scene, pasted[0]) == host);
    CHECK(scene.Registry().get<NameComponent>(pasted[0]).name == "Body");
    CHECK(scene.Registry().get<MeshRendererComponent>(pasted[0]).mesh == "/Game/SM_Body");
    CHECK(scene.Registry().get<TransformComponent>(pasted[0]).position.x == 1.0f);
    REQUIRE(ChildrenOf(scene, pasted[0]).size() == 1);

    auto again = clipboard.Paste(scene, kNullEntity, {});
    REQUIRE(again.size() == 1);
    CHECK(again[0] != pasted[0]);
    CHECK(scene.Registry().get<NameComponent>(again[0]).name == "Body 1");
}

TEST_CASE("Cut paste moves the actor and keeps its name", "[scene][hierarchy]") {
    Scene scene;
    Entity body = scene.CreateEntity("Body");
    Entity other = scene.CreateEntity("Other");
    ActorClipboard clipboard;
    REQUIRE(clipboard.Cut(scene, {body}));
    auto pasted = clipboard.Paste(scene, other, clipboard.CutSources());
    REQUIRE(pasted.size() == 1);
    CHECK(scene.Registry().get<NameComponent>(pasted[0]).name == "Body");
    CHECK(ParentOf(scene, pasted[0]) == other);
    DestroySubtree(scene, body);
    CHECK_FALSE(scene.IsValid(body));
    CHECK(scene.IsValid(pasted[0]));
}

TEST_CASE("Selection roots drop nested actors", "[scene][hierarchy]") {
    Scene scene;
    Entity root = scene.CreateEntity("Root");
    Entity child = scene.CreateEntity("Child");
    REQUIRE(SetParent(scene, child, root));
    auto roots = SelectionRoots(scene, {child, root});
    REQUIRE(roots.size() == 1);
    CHECK(roots[0] == root);
}

TEST_CASE("Clipboard keeps missing-plugin data and ownership through paste", "[scene][hierarchy]") {
    Scene scene;
    auto source = scene.CreateEntity("Body");
    auto& missing = scene.Registry().emplace<UnresolvedComponents>(source);
    missing.payloads.push_back({"Curve", "Studio.Curves", SerialValue(23)});
    missing.keyOrder = {"Curve", "Name", "Transform"};
    ActorClipboard clipboard;
    REQUIRE(clipboard.Copy(scene, {source}));
    auto pasted = clipboard.Paste(scene, kNullEntity, {});
    REQUIRE(pasted.size() == 1);
    auto* kept = scene.Registry().try_get<UnresolvedComponents>(pasted.front());
    REQUIRE(kept);
    REQUIRE(kept->Find("Curve"));
    CHECK(kept->Find("Curve")->owner == "Studio.Curves");
    CHECK(kept->Find("Curve")->payload.AsInt() == 23);
    CHECK(kept->keyOrder.front() == "Curve");
}
