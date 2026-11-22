#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Luma/Editor/EditorIcons.h"
#include "Luma/Editor/Panels/WorldOutliner.h"
#include "Luma/Scene/EntityTemplates.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Slate/Menu.h"

using namespace Luma;
using Luma::Editor::EditorImages;
using Luma::Editor::Panels::PanelContext;
using Luma::Editor::Panels::WorldOutlinerPanel;
using Luma::Slate::MenuEntry;

namespace {

const MenuEntry* FindChild(const std::vector<MenuEntry>& entries, std::string_view label) {
    for (const MenuEntry& e : entries) {
        if (e.label == label) return &e;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Place Actors menu is built from the entity templates", "[outliner][registry]") {
    WorldOutlinerPanel outliner;
    Scene scene;
    Entity selected = kNullEntity;
    std::vector<std::string> created;
    PanelContext ctx;
    ctx.scene = &scene;
    ctx.selected = &selected;
    ctx.onCreateEntity = [&created](const std::string& id) { created.push_back(id); };

    const std::vector<MenuEntry> menu = outliner.BuildPlaceActorsMenu(ctx);
    const MenuEntry* lights = FindChild(menu, "Lights");
    REQUIRE(lights != nullptr);
    REQUIRE(lights->kind == MenuEntry::Kind::SubMenu);
    const MenuEntry* spot = FindChild(lights->children, "Spot Light");
    REQUIRE(spot != nullptr);
    spot->action();
    REQUIRE(created == std::vector<std::string>{"SpotLight"});
    for (const char* cat : {"Basic", "Geometry", "Lights", "Environment", "Volumes"}) {
        INFO(cat);
        REQUIRE(FindChild(menu, cat) != nullptr);
    }

    // A plugin template appears in its own category.
    EntityTemplate beacon;
    beacon.id = "Beacon";
    beacon.displayName = "Beacon";
    beacon.category = "Signals";
    beacon.owner = "Test.Beacons";
    beacon.create = [](Scene& s, const EntityTemplateContext&) { return s.CreateEntity("Beacon"); };
    {
        RegistrationToken t = EntityTemplates().Register(beacon);
        const auto withPlugin = outliner.BuildPlaceActorsMenu(ctx);
        const MenuEntry* signals = FindChild(withPlugin, "Signals");
        REQUIRE(signals != nullptr);
        REQUIRE(FindChild(signals->children, "Beacon") != nullptr);
    }
    REQUIRE(FindChild(outliner.BuildPlaceActorsMenu(ctx), "Signals") == nullptr);
}

TEST_CASE("Entity menu disables Duplicate for one-per-scene holders", "[outliner][registry]") {
    WorldOutlinerPanel outliner;
    Scene scene;
    Entity env = Instantiate(scene, *EntityTemplates().Find("Environment"), {1});
    Entity cube = Instantiate(scene, *EntityTemplates().Find("Cube"), {1});
    Entity selected = kNullEntity;
    PanelContext ctx;
    ctx.scene = &scene;
    ctx.selected = &selected;
    ctx.onDuplicateEntity = [](Entity) {};
    ctx.onDeleteEntity = [](Entity) {};
    ctx.onFocusEntity = [](Entity) {};
    const auto envMenu = outliner.BuildEntityMenu(env, ctx);
    const MenuEntry* dup = FindChild(envMenu, "Duplicate");
    REQUIRE_FALSE(dup->enabled);
    REQUIRE(dup->tooltip.find("Sky & Environment") != std::string::npos);
    REQUIRE(FindChild(outliner.BuildEntityMenu(cube, ctx), "Duplicate")->enabled);
}

TEST_CASE("Editor images are registered by name with a glyph fallback", "[outliner][registry]") {
    REQUIRE(Editor::MenuIconByName("Light").glyph == Slate::Icon::Light);
    REQUIRE(Editor::MenuIconByName("NoSuchIcon").Empty());
    {
        RegistrationToken t = EditorImages().Register("TestImage", 42, Slate::Icon::Cube, "Test");
        REQUIRE(t.IsActive());
        REQUIRE(EditorImages().Find("TestImage") == 42);
        const Slate::MenuIcon icon = Editor::MenuIconByName("TestImage");
        REQUIRE(icon.texture == 42);
        REQUIRE(icon.glyph == Slate::Icon::Cube);
        std::string error;
        REQUIRE_FALSE(EditorImages().Register("TestImage", 7, Slate::Icon::None, "Test", &error)
                          .IsActive());
        REQUIRE(error.find("TestImage") != std::string::npos);
    }
    REQUIRE(EditorImages().Find("TestImage") == 0);
}
