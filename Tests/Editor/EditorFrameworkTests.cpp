#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Luma/Editor/MenuRegistry.h"
#include "Luma/Editor/PanelRegistry.h"
#include "Luma/Editor/SettingsPageRegistry.h"
#include "Luma/Slate/DockSpace.h"
#include "Luma/Slate/Menu.h"

using namespace Luma;
using namespace Luma::Editor;
using Luma::Slate::MenuEntry;

namespace {

MenuEntryDesc Entry(std::string id, std::string menu, std::string section, std::string label,
                    i32 order, std::function<void()> run = [] {}) {
    MenuEntryDesc e;
    e.id = std::move(id);
    e.menu = std::move(menu);
    e.section = std::move(section);
    e.label = std::move(label);
    e.order = order;
    e.owner = "Test";
    e.execute = std::move(run);
    return e;
}

std::vector<std::string> Labels(const std::vector<MenuEntry>& entries) {
    std::vector<std::string> out;
    for (const MenuEntry& e : entries) {
        if (e.kind == MenuEntry::Kind::Heading) out.push_back("#" + e.label);
        if (e.kind == MenuEntry::Kind::Action) out.push_back(e.label);
    }
    return out;
}

PanelDesc Panel(std::string id, std::string dockNextTo = {},
                Slate::DockDir dir = Slate::DockDir::Center, bool openByDefault = true) {
    PanelDesc p;
    p.id = id;
    p.title = id;
    p.owner = "Test";
    p.dockNextTo = std::move(dockNextTo);
    p.dockDir = dir;
    p.openByDefault = openByDefault;
    p.draw = [](Slate::Context&, const Slate::Rect&, Panels::PanelContext&) {};
    return p;
}

}  // namespace

TEST_CASE("Menus are ordered and entries grouped by section", "[editor][framework]") {
    MenuRegistry reg;
    RegistrationSet set;
    set.Add(reg.RegisterMenu({"Window", "Window", 900, "Engine"}));
    set.Add(reg.RegisterMenu({"File", "File", 0, "Engine"}));
    set.Add(reg.RegisterMenu({"Tools", "Tools", 800, "Engine"}));
    int saved = 0;
    set.Add(reg.RegisterEntry(Entry("File.Save", "File", "Scene", "Save Scene", 10,
                                    [&saved] { ++saved; })));
    set.Add(reg.RegisterEntry(Entry("File.Reveal", "File", "Project", "Show Project", 100)));
    set.Add(reg.RegisterEntry(Entry("File.SaveAll", "File", "Scene", "Save All", 20)));

    std::vector<std::string> bar;
    for (const MenuDesc* m : reg.Menus()) bar.push_back(m->id);
    REQUIRE(bar == std::vector<std::string>{"File", "Tools", "Window"});
    REQUIRE(reg.HasEntries("File"));
    REQUIRE_FALSE(reg.HasEntries("Tools"));  // empty menus are hidden from the bar

    const auto file = reg.Build("File");
    REQUIRE(Labels(file) ==
            std::vector<std::string>{"#Scene", "Save Scene", "Save All", "#Project", "Show Project"});
    for (const MenuEntry& e : file) {
        if (e.label == "Save Scene") e.action();
    }
    REQUIRE(saved == 1);
}

TEST_CASE("Menu entries honour enabled and checked callbacks", "[editor][framework]") {
    MenuRegistry reg;
    RegistrationToken m = reg.RegisterMenu({"Edit", "Edit", 100, "Engine"});
    bool enabled = false;
    MenuEntryDesc d = Entry("Edit.Dup", "Edit", "Selection", "Duplicate", 0);
    d.isEnabled = [&enabled] { return enabled; };
    d.isChecked = [] { return true; };
    d.shortcut = "Ctrl+D";
    d.tooltip = "Copy it.";
    RegistrationToken e = reg.RegisterEntry(d);
    auto built = reg.Build("Edit");
    REQUIRE(built.size() == 2);
    REQUIRE_FALSE(built[1].enabled);
    REQUIRE(built[1].checked);
    REQUIRE(built[1].shortcut == "Ctrl+D");
    REQUIRE(built[1].tooltip == "Copy it.");
    enabled = true;
    REQUIRE(reg.Build("Edit")[1].enabled);
}

TEST_CASE("Menu registry rejects duplicates and unknown menus, and unregisters",
          "[editor][framework]") {
    MenuRegistry reg;
    RegistrationToken file = reg.RegisterMenu({"File", "File", 0, "Engine"});
    std::string error;
    REQUIRE_FALSE(reg.RegisterMenu({"File", "File", 5, "Plugin"}, &error).IsActive());
    REQUIRE(error.find("File") != std::string::npos);
    REQUIRE_FALSE(reg.RegisterEntry(Entry("X.Y", "Nope", "S", "L", 0), &error).IsActive());
    REQUIRE(error.find("Nope") != std::string::npos);

    RegistrationToken a = reg.RegisterEntry(Entry("File.A", "File", "S", "A", 0));
    REQUIRE_FALSE(reg.RegisterEntry(Entry("File.A", "File", "S", "A2", 0), &error).IsActive());
    a.Reset();
    REQUIRE_FALSE(reg.HasEntries("File"));

    MenuEntryDesc noRun = Entry("File.B", "File", "S", "B", 0);
    noRun.execute = nullptr;
    REQUIRE_FALSE(reg.RegisterEntry(noRun, &error).IsActive());
}

TEST_CASE("Dynamic menu sections fill entries at build time", "[editor][framework]") {
    MenuRegistry reg;
    RegistrationToken w = reg.RegisterMenu({"Window", "Window", 900, "Engine"});
    std::vector<std::string> names = {"One", "Two"};
    MenuSectionDesc s;
    s.id = "Window.Panels";
    s.menu = "Window";
    s.section = "Panels";
    s.order = 0;
    s.owner = "Engine";
    s.fill = [&names](Slate::MenuBuilder& b) {
        for (const std::string& n : names) b.AddEntry(n, {}, [] {});
    };
    RegistrationToken t = reg.RegisterSection(s);
    RegistrationToken e = reg.RegisterEntry(Entry("Window.Reset", "Window", "Layout", "Reset", 100));
    REQUIRE(Labels(reg.Build("Window")) ==
            std::vector<std::string>{"#Panels", "One", "Two", "#Layout", "Reset"});
    names.push_back("Three");
    REQUIRE(Labels(reg.Build("Window")).size() == 6);
}

TEST_CASE("Panel registry validates and unregisters", "[editor][framework]") {
    PanelRegistry reg;
    RegistrationToken a = reg.Register(Panel("viewport"));
    REQUIRE(a.IsActive());
    std::string error;
    REQUIRE_FALSE(reg.Register(Panel("viewport"), &error).IsActive());
    REQUIRE(error.find("viewport") != std::string::npos);
    PanelDesc noDraw = Panel("x");
    noDraw.draw = nullptr;
    REQUIRE_FALSE(reg.Register(noDraw, &error).IsActive());
    a.Reset();
    REQUIRE(reg.Find("viewport") == nullptr);
}

TEST_CASE("The panel host builds the default layout and toggles panels",
          "[editor][framework]") {
    PanelRegistry reg;
    RegistrationSet set;
    set.Add(reg.Register(Panel("viewport")));
    set.Add(reg.Register(Panel("console", "viewport", Slate::DockDir::Down)));
    set.Add(reg.Register(Panel("stats", "viewport", Slate::DockDir::Right, false)));
    Slate::DockSpace dock;
    EditorPanelHost host(reg, dock, [] { return Panels::PanelContext{}; });
    host.BuildLayout();
    REQUIRE(host.IsOpen("viewport"));
    REQUIRE(host.IsOpen("console"));
    REQUIRE_FALSE(host.IsOpen("stats"));
    REQUIRE(dock.HasPanel("console"));

    host.Open("stats");
    REQUIRE(host.IsOpen("stats"));
    host.Close("console");
    REQUIRE_FALSE(host.IsOpen("console"));
    REQUIRE(host.ClosedPanels() == std::vector<std::string>{"console"});

    // A panel registered later (a plugin) opens on the next sync; one that goes
    // away is closed.
    RegistrationToken late = reg.Register(Panel("late", "viewport", Slate::DockDir::Left));
    host.Sync();
    REQUIRE(host.IsOpen("late"));
    late.Reset();
    host.Sync();
    REQUIRE_FALSE(host.IsOpen("late"));
}

TEST_CASE("Panel open state persists by panel id", "[editor][framework]") {
    PanelRegistry reg;
    RegistrationSet set;
    set.Add(reg.Register(Panel("viewport")));
    set.Add(reg.Register(Panel("console", "viewport", Slate::DockDir::Down)));
    set.Add(reg.Register(Panel("stats", "viewport", Slate::DockDir::Right, false)));
    SerialValue state;
    {
        Slate::DockSpace dock;
        EditorPanelHost host(reg, dock, [] { return Panels::PanelContext{}; });
        host.BuildLayout();
        host.Close("console");
        host.Open("stats");
        state = host.SaveState();
    }
    Slate::DockSpace dock;
    EditorPanelHost host(reg, dock, [] { return Panels::PanelContext{}; });
    host.LoadState(state);
    host.BuildLayout();
    REQUIRE(host.IsOpen("viewport"));
    REQUIRE_FALSE(host.IsOpen("console"));
    REQUIRE(host.IsOpen("stats"));

    host.ResetLayout();  // back to the registered defaults
    REQUIRE(host.IsOpen("console"));
    REQUIRE_FALSE(host.IsOpen("stats"));
}

TEST_CASE("A panel that can't be closed stays open", "[editor][framework]") {
    PanelRegistry reg;
    PanelDesc viewport = Panel("viewport");
    viewport.closable = false;
    RegistrationToken t = reg.Register(viewport);
    Slate::DockSpace dock;
    EditorPanelHost host(reg, dock, [] { return Panels::PanelContext{}; });
    host.BuildLayout();
    host.Close("viewport");
    REQUIRE(host.IsOpen("viewport"));
}

TEST_CASE("Settings pages are validated and listed by category", "[editor][framework]") {
    SettingsPageRegistry reg;
    auto page = [](std::string id, std::string category, std::string title) {
        SettingsPageDesc p;
        p.id = std::move(id);
        p.category = std::move(category);
        p.title = std::move(title);
        p.owner = "Test";
        p.configFile = "Game.json";
        p.load = [](const SerialValue&) {};
        p.save = [] { return SerialValue::MakeObject(); };
        p.draw = [](Slate::Context&, const Slate::Rect&) { return false; };
        return p;
    };
    RegistrationSet set;
    set.Add(reg.Register(page("Project.General", "Project", "General")));
    set.Add(reg.Register(page("Plugins", "Plugins", "Plugins")));
    set.Add(reg.Register(page("Project.Maps", "Project", "Maps & Modes")));
    std::string error;
    REQUIRE_FALSE(reg.Register(page("Plugins", "X", "Y"), &error).IsActive());
    SettingsPageDesc bad = page("Bad", "Project", "Bad");
    bad.configFile = "../escape.json";
    REQUIRE_FALSE(reg.Register(bad, &error).IsActive());
    REQUIRE(error.find("configFile") != std::string::npos);
    SettingsPageDesc noSave = page("NoSave", "Project", "No Save");
    noSave.save = nullptr;
    REQUIRE_FALSE(reg.Register(noSave, &error).IsActive());

    REQUIRE(reg.Categories() == std::vector<std::string>{"Project", "Plugins"});
    auto project = reg.PagesIn("Project");
    REQUIRE(project.size() == 2);
    REQUIRE(project[0]->id == "Project.General");
    set.Reset();
    REQUIRE(reg.All().empty());
}

TEST_CASE("Menu entries can be executed by id", "[editor][framework]") {
    MenuRegistry reg;
    RegistrationToken m = reg.RegisterMenu({"Edit", "Edit", 100, "Engine"});
    int runs = 0;
    bool enabled = true;
    MenuEntryDesc d = Entry("Edit.Run", "Edit", "S", "Run", 0, [&runs] { ++runs; });
    d.isEnabled = [&enabled] { return enabled; };
    RegistrationToken e = reg.RegisterEntry(d);
    std::string error;
    REQUIRE(reg.Execute("Edit.Run", &error));
    REQUIRE(runs == 1);
    enabled = false;
    REQUIRE_FALSE(reg.Execute("Edit.Run", &error));
    REQUIRE(error.find("disabled") != std::string::npos);
    REQUIRE_FALSE(reg.Execute("Edit.Nope", &error));
    REQUIRE(runs == 1);
}
