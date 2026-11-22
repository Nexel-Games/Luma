#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "Luma/Editor/InspectorDrawers.h"
#include "Luma/Editor/Panels/Inspector.h"
#include "Luma/Scene/ComponentReflection.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Menu.h"

using namespace Luma;
using Luma::Editor::BuildReflectedRows;
using Luma::Editor::InspectorDrawer;
using Luma::Editor::InspectorDrawers;
using Luma::Editor::ReflectedRow;
using Luma::Editor::Panels::InspectorPanel;
using Luma::Editor::Panels::PanelContext;
using Luma::Slate::MenuEntry;

namespace {

struct BeaconComponent {
    float range = 10.0f;
    int channel = 3;
    bool active = true;
    std::string label = "Beacon";
    Math::Vec3 tint{1.0f, 0.5f, 0.25f};
    int mode = 1;
};

RegistrationToken RegisterBeacon(bool onePerScene = false) {
    ComponentDesc d;
    d.id = "Beacon";
    d.displayName = "Beacon";
    d.category = "Signals";
    d.tooltip = "Broadcasts on a channel.";
    d.icon = "Light";
    d.owner = "Test.Beacons";
    d.onePerScene = onePerScene;
    return Components().Register<BeaconComponent>(std::move(d));
}

class NullRenderer final : public Renderer {
public:
    void OnResize(u32, u32) override {}
    void SetClearColor(const ClearColor&) override {}
    bool BeginFrame() override { return true; }
    void EndFrame() override {}
    void DrawUI(const UIDrawData&) override {}
    TextureHandle CreateTexture(u32, u32, const void*) override { return 1; }
    void DestroyTexture(TextureHandle) override {}
    void CaptureFrame(const std::string&) override {}
    void WaitIdle() override {}
};

void InitUi(Slate::Context& ui, NullRenderer& renderer) {
    Slate::Typography typo;
    typo.uiRegular = "C:/Windows/Fonts/segoeui.ttf";
    typo.uiMedium = typo.uiSemiBold = typo.uiBold = typo.mono = typo.uiRegular;
    ui.Init(renderer, typo);
}

// Flattens a menu to its action entries (with the heading they sit under).
struct FlatEntry {
    std::string section;
    MenuEntry item;
    const MenuEntry* entry = nullptr;  // points at `item`
};
std::vector<FlatEntry> Flatten(const std::vector<MenuEntry>& menu) {
    std::vector<FlatEntry> out;
    std::string section;
    for (const MenuEntry& m : menu) {
        if (m.kind == MenuEntry::Kind::Heading) section = m.label;
        if (m.kind == MenuEntry::Kind::Action) out.push_back({section, m, nullptr});
    }
    for (FlatEntry& f : out) f.entry = &f.item;
    return out;
}
const FlatEntry* FindEntry(const std::vector<FlatEntry>& flat, std::string_view label) {
    for (const FlatEntry& f : flat) {
        if (f.entry->label == label) return &f;
    }
    return nullptr;
}

}  // namespace

namespace Luma {
template <>
const TypeInfo<BeaconComponent>& GetTypeInfo<BeaconComponent>() {
    static const TypeInfo<BeaconComponent> info = [] {
        TypeBuilder<BeaconComponent> b("Beacon");
        b.Property("range", &BeaconComponent::range).Category("Signal").Range(0.0, 100.0);
        b.Property("channel", &BeaconComponent::channel).Category("Signal");
        b.Property("active", &BeaconComponent::active).Category("Signal");
        b.Property("label", &BeaconComponent::label).Category("Display");
        b.Property("tint", &BeaconComponent::tint).Category("Display").Color();
        b.Property("mode", &BeaconComponent::mode).Category("Display").Enum({"Off", "Pulse", "Steady"});
        return b.Build();
    }();
    return info;
}
}  // namespace Luma

TEST_CASE("Add Component lists the registered user-addable components by category",
          "[inspector][registry]") {
    InspectorPanel inspector;
    Scene scene;
    Entity e = scene.CreateEntity("Actor");
    const auto flat = Flatten(inspector.BuildAddComponentMenu(&scene, e));

    REQUIRE(FindEntry(flat, "Name") == nullptr);       // structural, not addable
    REQUIRE(FindEntry(flat, "Transform") == nullptr);
    const FlatEntry* mesh = FindEntry(flat, "Mesh Renderer");
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->section == "Rendering");
    REQUIRE(mesh->entry->enabled);
    REQUIRE(mesh->entry->tooltip == Components().Find("MeshRenderer")->tooltip);
    for (const char* light : {"Directional Light", "Point Light", "Spot Light", "Tube Light"}) {
        INFO(light);
        const FlatEntry* f = FindEntry(flat, light);
        REQUIRE(f != nullptr);
        REQUIRE(f->section == "Lighting");
    }
    REQUIRE(FindEntry(flat, "Sky & Environment") != nullptr);
    REQUIRE(FindEntry(flat, "Post Process Volume") != nullptr);
    REQUIRE(FindEntry(flat, "Beacon") == nullptr);

    // A plugin component appears in its own category, and goes away again.
    {
        RegistrationToken beacon = RegisterBeacon();
        const auto withPlugin = Flatten(inspector.BuildAddComponentMenu(&scene, e));
        const FlatEntry* b = FindEntry(withPlugin, "Beacon");
        REQUIRE(b != nullptr);
        REQUIRE(b->section == "Signals");
        REQUIRE(b->entry->tooltip == "Broadcasts on a channel.");
        b->entry->action();
        REQUIRE(scene.Registry().all_of<BeaconComponent>(e));
    }
    REQUIRE(FindEntry(Flatten(inspector.BuildAddComponentMenu(&scene, e)), "Beacon") == nullptr);
}

TEST_CASE("Add Component enforces one-per-entity and one-per-scene", "[inspector][registry]") {
    InspectorPanel inspector;
    Scene scene;
    Entity a = scene.CreateEntity("A");
    Entity b = scene.CreateEntity("B");
    Components().Find("Light")->variants[0].apply(scene, a);
    Components().Find("Environment")->add(scene, b);

    const auto flat = Flatten(inspector.BuildAddComponentMenu(&scene, a));
    const FlatEntry* point = FindEntry(flat, "Point Light");
    REQUIRE_FALSE(point->entry->enabled);
    REQUIRE(point->entry->tooltip.find("one per entity") != std::string::npos);
    const FlatEntry* env = FindEntry(flat, "Sky & Environment");
    REQUIRE_FALSE(env->entry->enabled);
    REQUIRE(env->entry->tooltip.find("one per scene") != std::string::npos);
    REQUIRE(FindEntry(flat, "Camera")->entry->enabled);

    // Picking a variant adds the configured component.
    Entity c = scene.CreateEntity("C");
    const auto forC = Flatten(inspector.BuildAddComponentMenu(&scene, c));
    FindEntry(forC, "Spot Light")->entry->action();
    REQUIRE(scene.Registry().get<LightComponent>(c).type == LightType::Spot);
}

TEST_CASE("Component header menu: reset everywhere, remove only user-addable",
          "[inspector][registry]") {
    InspectorPanel inspector;
    Scene scene;
    Entity e = scene.CreateEntity("A");
    Components().Find("Camera")->add(scene, e);
    scene.Registry().get<CameraComponent>(e).fovYDegrees = 33.0f;

    auto camMenu = Flatten(inspector.BuildComponentMenu(&scene, e, *Components().Find("Camera")));
    const FlatEntry* reset = FindEntry(camMenu, "Reset to Defaults");
    REQUIRE(reset != nullptr);
    reset->entry->action();
    REQUIRE(scene.Registry().get<CameraComponent>(e).fovYDegrees == CameraComponent{}.fovYDegrees);
    FindEntry(camMenu, "Remove Component")->entry->action();
    REQUIRE_FALSE(scene.Registry().all_of<CameraComponent>(e));

    auto tfMenu =
        Flatten(inspector.BuildComponentMenu(&scene, e, *Components().Find("Transform")));
    REQUIRE_FALSE(FindEntry(tfMenu, "Remove Component")->entry->enabled);
}

TEST_CASE("The default drawer builds rows from reflection metadata", "[inspector][registry]") {
    RegistrationToken beacon = RegisterBeacon();
    Scene scene;
    Entity e = scene.CreateEntity("A");
    const ComponentDesc& desc = *Components().Find("Beacon");
    desc.add(scene, e);
    SerialValue payload;
    desc.write(scene, e, payload);
    const std::vector<ReflectedRow> rows = BuildReflectedRows(desc, payload);
    REQUIRE(rows.size() == 6);
    REQUIRE(rows[0].label == "Range");
    REQUIRE(rows[0].kind == ReflectedRow::Kind::Float);
    REQUIRE(rows[0].category == "Signal");
    REQUIRE(rows[1].kind == ReflectedRow::Kind::Int);
    REQUIRE(rows[2].kind == ReflectedRow::Kind::Bool);
    REQUIRE(rows[3].kind == ReflectedRow::Kind::String);
    REQUIRE(rows[4].kind == ReflectedRow::Kind::Color);
    REQUIRE(rows[5].kind == ReflectedRow::Kind::Enum);
    REQUIRE(rows[5].category == "Display");
}

TEST_CASE("Drawing a registered component without a custom drawer uses reflection",
          "[inspector][registry]") {
    RegistrationToken beacon = RegisterBeacon();
    REQUIRE(InspectorDrawers().Find("Beacon") == nullptr);
    Scene scene;
    Entity e = scene.CreateEntity("A");
    Components().Find("Beacon")->add(scene, e);

    NullRenderer renderer;
    Slate::Context ui;
    InitUi(ui, renderer);
    InspectorPanel inspector;
    PanelContext ctx;
    ctx.scene = &scene;
    ctx.selected = &e;
    ui.BeginFrame(1280, 720, 0.016f);
    inspector.Draw(ui, {0, 0, 400, 720}, ctx);
    ui.EndFrame();
    // One section per drawn component; the reflection drawer produced rows.
    const auto& drawn = inspector.LastDrawnSections();
    REQUIRE(std::find(drawn.begin(), drawn.end(), "Beacon") != drawn.end());
    REQUIRE(std::find(drawn.begin(), drawn.end(), "Transform") != drawn.end());
    REQUIRE(std::find(drawn.begin(), drawn.end(), "Name") == drawn.end());  // header shows it
}

TEST_CASE("Inspector drawer registry rejects duplicates and unregisters",
          "[inspector][registry]") {
    InspectorPanel inspector;  // ensures the built-in drawers are registered
    for (const char* id : {"Name", "Transform", "MeshRenderer", "Camera", "Light",
                           "Environment", "PostProcess"}) {
        INFO(id);
        REQUIRE(InspectorDrawers().Find(id) != nullptr);
    }
    InspectorDrawer d;
    d.componentId = "Transform";
    d.owner = "Test";
    d.draw = [](Editor::InspectorRows&) {};
    std::string error;
    RegistrationToken dup = InspectorDrawers().Register(d, &error);
    REQUIRE_FALSE(dup.IsActive());
    REQUIRE(error.find("Transform") != std::string::npos);

    d.componentId = "Beacon";
    {
        RegistrationToken t = InspectorDrawers().Register(d);
        REQUIRE(t.IsActive());
        REQUIRE(InspectorDrawers().Find("Beacon") != nullptr);
    }
    REQUIRE(InspectorDrawers().Find("Beacon") == nullptr);
}

TEST_CASE("Missing-plugin components show a kept-data row and need confirmation to remove",
          "[inspector][registry]") {
    std::string saved;
    {
        RegistrationToken beacon = RegisterBeacon();
        Scene scene;
        Entity e = scene.CreateEntity("Tower");
        Components().Find("Beacon")->add(scene, e);
        saved = SceneSerializer::SaveToString(scene);
    }
    Scene scene;
    REQUIRE(SceneSerializer::LoadFromString(scene, saved));
    Entity tower = kNullEntity;
    for (auto e : scene.Registry().view<NameComponent>()) tower = e;
    const auto& unresolved = scene.Registry().get<UnresolvedComponents>(tower);
    REQUIRE(Editor::MissingComponentLabel(unresolved.payloads[0]) ==
            "Beacon (plugin Test.Beacons not loaded) \xE2\x80\x94 data kept");

    InspectorPanel inspector;
    inspector.RequestRemoveUnresolved(tower, "Beacon");
    REQUIRE(inspector.PendingRemoveUnresolved() == "Beacon");
    inspector.CancelRemoveUnresolved();
    REQUIRE(inspector.PendingRemoveUnresolved().empty());
    REQUIRE(scene.Registry().all_of<UnresolvedComponents>(tower));

    inspector.RequestRemoveUnresolved(tower, "Beacon");
    inspector.ConfirmRemoveUnresolved(scene);
    REQUIRE_FALSE(scene.Registry().all_of<UnresolvedComponents>(tower));
    REQUIRE(SceneSerializer::SaveToString(scene).find("Beacon\"") == std::string::npos);
}

TEST_CASE("Add Component blocks a type whose data is kept for a missing plugin",
          "[inspector][registry]") {
    Scene scene;
    Entity e = scene.CreateEntity("A");
    UnresolvedComponents kept;
    kept.payloads.push_back({"Camera", "Engine", SerialValue::MakeObject()});
    scene.Registry().emplace<UnresolvedComponents>(e, kept);
    InspectorPanel inspector;
    const auto flat = Flatten(inspector.BuildAddComponentMenu(&scene, e));
    REQUIRE_FALSE(FindEntry(flat, "Camera")->entry->enabled);
}
