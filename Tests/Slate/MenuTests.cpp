// Slate popup menu tests: MenuBuilder structure, SearchMenu, and the
// MenuHost interaction loop driven through Context (overlay draw in
// EndFrame, input blocking, click / keyboard activation, submenus).

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Menu.h"

using Luma::f32;
using Luma::Slate::Context;
using Luma::Slate::MenuBuilder;
using Luma::Slate::MenuEntry;
using Luma::Slate::MenuEntryOptions;
using Luma::Slate::MenuOptions;
using Luma::Slate::Rect;

namespace {

class NullRenderer final : public Luma::Renderer {
public:
    void OnResize(Luma::u32, Luma::u32) override {}
    void SetClearColor(const Luma::ClearColor&) override {}
    bool BeginFrame() override { return true; }
    void EndFrame() override {}
    void DrawUI(const Luma::UIDrawData&) override {}
    Luma::TextureHandle CreateTexture(Luma::u32, Luma::u32, const void*) override {
        return 1;
    }
    void DestroyTexture(Luma::TextureHandle) override {}
    void CaptureFrame(const std::string&) override {}
    void WaitIdle() override {}
};

Luma::Slate::Typography TestTypography() {
    Luma::Slate::Typography t;
    t.uiRegular = "C:/Windows/Fonts/segoeui.ttf";
    t.uiMedium = t.uiRegular;
    t.uiSemiBold = t.uiRegular;
    t.uiBold = t.uiRegular;
    t.mono = t.uiRegular;
    return t;
}

struct Fixture {
    NullRenderer renderer;
    Context ui;
    Fixture() { ui.Init(renderer, TestTypography()); }

    // One frame; `widgets` runs between Begin/End (the "panels").
    template <typename Fn>
    void Frame(Fn&& widgets) {
        ui.BeginFrame(1280, 720, 0.0166f);
        widgets();
        ui.EndFrame();
    }
    void Frame() {
        Frame([] {});
    }
    void Move(f32 x, f32 y) {
        ui.OnMouseMove(x, y);
        Frame();
    }
    // Press and release on separate frames (like a real click).
    void Click(f32 x, f32 y, int button = 0) {
        ui.OnMouseMove(x, y);
        ui.OnMouseButton(button, true);
        Frame();
        ui.OnMouseButton(button, false);
        Frame();
    }
    void Key(int glfwKey) {
        ui.OnKey(glfwKey, true);
        Frame();
    }
};

Luma::Slate::Vec2 Center(const Rect& r) { return {r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }

constexpr int kKeyEscape = 256;
constexpr int kKeyEnter = 257;
constexpr int kKeyRight = 262;
constexpr int kKeyDown = 264;

}  // namespace

TEST_CASE("MenuBuilder separates sections and trims dangling separators",
          "[slate][menu]") {
    MenuBuilder b;
    b.BeginSection("Get");
    b.AddEntry("Import", {}, [] {});
    b.BeginSection("Create");
    b.AddEntry("New Folder", {}, [] {});
    b.AddSeparator();
    auto entries = b.Build();

    REQUIRE(entries.size() == 5);
    CHECK(entries[0].kind == MenuEntry::Kind::Heading);
    CHECK(entries[1].label == "Import");
    CHECK(entries[2].kind == MenuEntry::Kind::Separator);
    CHECK(entries[3].kind == MenuEntry::Kind::Heading);
    CHECK(entries[4].label == "New Folder");
    CHECK(b.Empty());
}

TEST_CASE("MenuBuilder disables empty submenus", "[slate][menu]") {
    MenuBuilder b;
    b.AddSubMenu("Nothing", {}, [](MenuBuilder&) {});
    b.AddSubMenu("Something", {}, [](MenuBuilder& s) { s.AddEntry("A", {}, [] {}); });
    auto entries = b.Build();
    REQUIRE(entries.size() == 2);
    CHECK_FALSE(entries[0].enabled);
    CHECK(entries[1].enabled);
    CHECK(entries[1].children.size() == 1);
}

TEST_CASE("SearchMenu flattens submenus and matches every term", "[slate][menu]") {
    MenuBuilder b;
    b.BeginSection("Place Actors");
    b.AddSubMenu("Lights", {}, [](MenuBuilder& s) {
        s.AddEntry("Point Light", {}, [] {});
        s.AddEntry("Spot Light", {}, [] {});
    });
    b.AddSubMenu("Geometry", {}, [](MenuBuilder& s) {
        s.AddEntry("Cube", {}, [] {});
        s.AddEntry("Sphere", {}, [] {});
    });
    auto entries = b.Build();

    auto hits = Luma::Slate::SearchMenu(entries, "light");
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].entry->label == "Point Light");
    CHECK(hits[0].group == "Lights");

    // Terms may match the group path too: "lights spot" -> Spot Light.
    hits = Luma::Slate::SearchMenu(entries, "lights spot");
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].entry->label == "Spot Light");

    // Case-insensitive; empty query -> no hits.
    CHECK(Luma::Slate::SearchMenu(entries, "CUBE").size() == 1);
    CHECK(Luma::Slate::SearchMenu(entries, "   ").empty());
    CHECK(Luma::Slate::SearchMenu(entries, "zzz").empty());
}

TEST_CASE("Clicking a menu entry runs its action and closes the menu",
          "[slate][menu]") {
    Fixture f;
    int fired = 0;
    MenuBuilder b;
    b.AddEntry("Do It", {}, [&] { ++fired; });
    f.Frame([&] { f.ui.OpenMenu(42, {100, 100}, b.Build()); });
    REQUIRE(f.ui.IsMenuOpen(42));
    f.Frame();  // lay out

    Rect row = f.ui.menus().EntryRect(0, "Do It");
    REQUIRE(row.w > 0.0f);
    auto c = Center(row);
    f.Click(c.x, c.y);
    CHECK(fired == 1);
    CHECK_FALSE(f.ui.AnyMenuOpen());
}

TEST_CASE("Releasing the right-click that opened a context menu does not select an item",
          "[slate][menu]") {
    Fixture f;
    int fired = 0;
    MenuBuilder b;
    b.AddEntry("Create", {}, [&] { ++fired; });

    f.ui.OnMouseMove(100, 100);
    f.ui.OnMouseButton(1, true);
    f.Frame([&] { f.ui.OpenMenu(42, {100, 100}, b.Build()); });
    REQUIRE(f.ui.IsMenuOpen(42));

    const Rect row = f.ui.menus().EntryRect(0, "Create");
    REQUIRE(row.w > 0.0f);
    const auto center = Center(row);
    f.Move(center.x, center.y);
    f.ui.OnMouseButton(1, false);
    f.Frame();

    CHECK(fired == 0);
    CHECK(f.ui.IsMenuOpen(42));

    f.Click(center.x, center.y);
    CHECK(fired == 1);
    CHECK_FALSE(f.ui.AnyMenuOpen());
}

TEST_CASE("An open menu blocks input to widgets and an outside click dismisses it",
          "[slate][menu]") {
    Fixture f;
    int fired = 0;
    MenuBuilder b;
    b.AddEntry("Item", {}, [&] { ++fired; });
    f.Frame([&] { f.ui.OpenMenu(7, {400, 300}, b.Build()); });
    f.Frame();

    // A button far from the menu: the click must not reach it.
    int buttonClicks = 0;
    auto panel = [&] {
        if (f.ui.Button(Context::ID("under"), {10, 10, 80, 24}, "Under")) ++buttonClicks;
    };
    f.ui.OnMouseMove(20, 20);
    f.ui.OnMouseButton(0, true);
    f.Frame(panel);
    CHECK_FALSE(f.ui.AnyMenuOpen());  // dismissed by the outside press
    f.ui.OnMouseButton(0, false);
    f.Frame(panel);
    CHECK(buttonClicks == 0);  // the dismissing click was consumed
    CHECK(fired == 0);
}

TEST_CASE("Keyboard navigation focuses, opens submenus and activates",
          "[slate][menu]") {
    Fixture f;
    std::string picked;
    MenuBuilder b;
    b.AddEntry("First", {}, [&] { picked = "First"; });
    b.AddSubMenu("More", {}, [&](MenuBuilder& s) {
        s.AddEntry("Nested", {}, [&] { picked = "Nested"; });
    });
    f.ui.OnMouseMove(5, 5);
    f.Frame([&] { f.ui.OpenMenu(1, {200, 200}, b.Build()); });
    f.Frame();

    f.Key(kKeyDown);   // -> First
    f.Key(kKeyDown);   // -> More
    f.Key(kKeyRight);  // open More, focus Nested
    REQUIRE(f.ui.menus().LevelCount() == 2);
    f.Key(kKeyEnter);
    CHECK(picked == "Nested");
    CHECK_FALSE(f.ui.AnyMenuOpen());
}

TEST_CASE("Hovering a submenu row opens it; Escape closes level by level",
          "[slate][menu]") {
    Fixture f;
    MenuBuilder b;
    b.AddSubMenu("Lights", {}, [](MenuBuilder& s) { s.AddEntry("Point", {}, [] {}); });
    b.AddEntry("Other", {}, [] {});
    f.Frame([&] { f.ui.OpenMenu(1, {200, 200}, b.Build()); });
    f.Frame();

    auto c = Center(f.ui.menus().EntryRect(0, "Lights"));
    f.Move(c.x, c.y);
    REQUIRE(f.ui.menus().LevelCount() == 2);
    // The submenu opens beside the parent, never on top of it.
    CHECK(f.ui.menus().LevelRect(1).x >= f.ui.menus().LevelRect(0).Right() - 4.0f);

    auto n = Center(f.ui.menus().EntryRect(1, "Point"));
    f.Move(n.x, n.y);
    f.Key(kKeyEscape);  // closes the submenu level
    CHECK(f.ui.menus().LevelCount() == 1);
    CHECK(f.ui.AnyMenuOpen());
    f.Key(kKeyEscape);  // closes the menu
    CHECK_FALSE(f.ui.AnyMenuOpen());
}

TEST_CASE("Disabled entries do not activate", "[slate][menu]") {
    Fixture f;
    int fired = 0;
    MenuBuilder b;
    MenuEntryOptions off;
    off.enabled = false;
    b.AddEntry("Locked", {}, [&] { ++fired; }, off);
    f.Frame([&] { f.ui.OpenMenu(1, {200, 200}, b.Build()); });
    f.Frame();
    auto c = Center(f.ui.menus().EntryRect(0, "Locked"));
    f.Click(c.x, c.y);
    CHECK(fired == 0);
    CHECK(f.ui.AnyMenuOpen());
}

TEST_CASE("Searchable menus filter as you type and Enter picks the top hit",
          "[slate][menu]") {
    Fixture f;
    std::string picked;
    MenuBuilder b;
    b.AddSubMenu("Lights", {}, [&](MenuBuilder& s) {
        s.AddEntry("Point Light", {}, [&] { picked = "Point Light"; });
    });
    b.AddEntry("Cube", {}, [&] { picked = "Cube"; });
    MenuOptions opts;
    opts.searchable = true;
    f.Frame([&] { f.ui.OpenMenu(1, {200, 200}, b.Build(), opts); });
    f.Frame();  // search field takes focus
    for (char ch : std::string("point")) f.ui.OnText(static_cast<Luma::u32>(ch));
    f.Frame();  // text lands in the field
    f.Frame();  // results rebuilt
    CHECK(f.ui.menus().SearchText() == "point");
    CHECK(f.ui.menus().EntryRect(0, "Point Light").w > 0.0f);
    CHECK(f.ui.menus().EntryRect(0, "Cube").w == 0.0f);
    f.Key(kKeyEnter);
    CHECK(picked == "Point Light");
}

TEST_CASE("Menus are clamped inside the display", "[slate][menu]") {
    Fixture f;
    MenuBuilder b;
    for (int i = 0; i < 8; ++i) b.AddEntry("Entry " + std::to_string(i), {}, [] {});
    f.Frame([&] { f.ui.OpenMenu(1, {1270, 715}, b.Build()); });
    f.Frame();
    Rect r = f.ui.menus().LevelRect(0);
    CHECK(r.x >= 0.0f);
    CHECK(r.y >= 0.0f);
    CHECK(r.Right() <= 1280.0f);
    CHECK(r.Bottom() <= 720.0f);
}

TEST_CASE("Modifier keys are level-tracked and cleared on focus loss",
          "[slate][menu][input]") {
    Fixture f;
    constexpr int kLeftCtrl = 341;
    constexpr int kKeyD = 68;
    f.ui.OnKey(kLeftCtrl, true);
    f.ui.OnKey(kKeyD, true);
    f.ui.BeginFrame(1280, 720, 0.0166f);
    CHECK(f.ui.ctrlDown());
    CHECK(f.ui.keyPressed(kKeyD));
    f.ui.EndFrame();
    f.ui.BeginFrame(1280, 720, 0.0166f);
    CHECK(f.ui.ctrlDown());            // still held
    CHECK_FALSE(f.ui.keyPressed(kKeyD));  // edge cleared
    f.ui.EndFrame();
    f.ui.OnKey(kLeftCtrl, false);
    CHECK_FALSE(f.ui.ctrlDown());
    f.ui.OnKey(kLeftCtrl, true);
    f.ui.ReleaseAllKeys();
    CHECK_FALSE(f.ui.ctrlDown());
}

TEST_CASE("rawMouse reports the real pointer while a menu blocks widgets",
          "[slate][menu][input]") {
    Fixture f;
    MenuBuilder b;
    b.AddEntry("Item", {}, [] {});
    f.Frame([&] { f.ui.OpenMenu(1, {600, 400}, b.Build()); });
    f.ui.OnMouseMove(30, 40);
    f.ui.BeginFrame(1280, 720, 0.0166f);
    CHECK(f.ui.inputBlocked());
    CHECK(f.ui.mouse().x < -1000.0f);   // widgets see no pointer
    CHECK(f.ui.rawMouse().x == 30.0f);  // menu bars still can
    CHECK(f.ui.rawMouse().y == 40.0f);
    f.ui.EndFrame();
}

TEST_CASE("Tooltips appear after the hover delay and hide on click",
          "[slate][tooltip]") {
    Fixture f;
    const Rect anchor{100, 100, 40, 24};
    auto hover = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            f.Frame([&] { f.ui.Tooltip(7, anchor, "Translate (W)\nMove the actor."); });
        }
    };
    f.ui.OnMouseMove(110, 110);
    hover(5);  // ~0.08 s: not yet
    const auto quiet = f.ui.drawList().vertexCount();
    hover(40);  // ~0.75 s: shown
    const auto shown = f.ui.drawList().vertexCount();
    CHECK(shown > quiet);

    // A click on the control hides its tip until the pointer leaves it.
    f.ui.OnMouseButton(0, true);
    hover(1);
    f.ui.OnMouseButton(0, false);
    hover(40);
    CHECK(f.ui.drawList().vertexCount() == quiet);

    // Moving to another control restarts the delay.
    const Rect other{300, 100, 40, 24};
    f.ui.OnMouseMove(310, 110);
    for (int i = 0; i < 40; ++i) {
        f.Frame([&] { f.ui.Tooltip(8, other, "Rotate (E)"); });
    }
    CHECK(f.ui.drawList().vertexCount() > quiet);
}

TEST_CASE("The click that opens a menu never selects an item in it", "[slate][menu]") {
    // A slot picker opens on mouse press. A long list fits neither below nor
    // above its anchor, so it is placed over the anchor -- under the cursor.
    // The release of that same click must not pick the row under it.
    Fixture f;
    int picked = -1;
    const Rect anchor{300, 340, 200, 22};
    auto widgets = [&] {
        if (f.ui.mousePressed(0) && anchor.Contains(f.ui.mouse()) && !f.ui.AnyMenuOpen()) {
            MenuBuilder b;
            for (int i = 0; i < 80; ++i) {
                b.AddEntry("Material " + std::to_string(i), {}, [&picked, i] { picked = i; });
            }
            f.ui.OpenMenuBelow(Context::ID("picker"), anchor, b.Build());
        }
    };
    f.ui.OnMouseMove(350, 351);
    f.ui.OnMouseButton(0, true);
    f.Frame(widgets);
    REQUIRE(f.ui.AnyMenuOpen());
    f.ui.OnMouseButton(0, false);
    f.Frame(widgets);
    CHECK(picked == -1);
    CHECK(f.ui.AnyMenuOpen());

    // A real click on a row still picks it.
    f.ui.OnMouseButton(0, true);
    f.Frame([] {});
    f.ui.OnMouseButton(0, false);
    f.Frame([] {});
    CHECK(picked >= 0);
}

TEST_CASE("Thumbnail icons are fetched while the menu is drawn", "[slate][menu]") {
    // Asset pickers show thumbnails that may still be generating: the entry
    // asks for its texture every frame, so it appears once it is ready.
    Fixture f;
    int calls = 0;
    Luma::TextureHandle ready = 0;
    MenuBuilder b;
    b.AddEntry("SM_Rock", Luma::Slate::MenuIcon::Thumbnail([&] { ++calls; return ready; },
                                                           Luma::Slate::Icon::StaticMesh),
               [] {});
    MenuOptions options;
    options.iconSize = 32.0f;
    f.ui.OpenMenuBelow(Context::ID("picker"), Rect{100, 100, 200, 22}, b.Build(), options);
    f.Frame();
    f.Frame();
    CHECK(calls >= 2);
    const auto before = f.ui.drawList().vertexCount();
    ready = 7;  // the thumbnail finished
    f.Frame();
    CHECK(calls >= 3);
    (void)before;
    CHECK(f.ui.AnyMenuOpen());
}

TEST_CASE("A long menu is capped at its maximum height and scrolls", "[slate][menu]") {
    Fixture f;
    MenuBuilder b;
    for (int i = 0; i < 80; ++i) b.AddEntry("Material " + std::to_string(i), {}, [] {});
    MenuOptions options;
    options.maxHeight = 300.0f;
    f.ui.OpenMenuBelow(Context::ID("picker"), Rect{100, 100, 200, 22}, b.Build(), options);
    f.Frame();
    const Rect r = f.ui.menus().LevelRect(0);
    CHECK(r.h <= 300.0f);
    CHECK(r.h > 200.0f);
    CHECK(r.y >= 122.0f);  // still opens below its anchor

    MenuBuilder c;
    for (int i = 0; i < 80; ++i) c.AddEntry("Material " + std::to_string(i), {}, [] {});
    f.ui.OpenMenuBelow(Context::ID("picker2"), Rect{100, 100, 200, 22}, c.Build());
    f.Frame();
    CHECK(f.ui.menus().LevelRect(0).h > 600.0f);  // no cap: as tall as the window allows
}
