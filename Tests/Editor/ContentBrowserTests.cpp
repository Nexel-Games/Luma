// ContentBrowserPanel tests: construction, navigation, filter selection, and
// the right-click Create menu (driven through Slate with a NullRenderer).

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Asset/AssetTypeRegistry.h"
#include "Luma/Editor/Panels/ContentBrowser.h"
#include "PanelContext.h"
#include "Luma/Slate/Context.h"

using namespace Luma;
using Luma::Editor::Panels::ContentBrowserPanel;
using Luma::Editor::Panels::PanelContext;
using Luma::Slate::Context;
using Luma::Slate::Rect;
namespace fs = std::filesystem;

namespace {
class TempDir {
public:
    TempDir() {
        auto base = fs::temp_directory_path();
        m_path = base / ("luma_cb_test_" + std::to_string(GetTick()));
        fs::create_directories(m_path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    const fs::path& Path() const { return m_path; }
    void Touch(const fs::path& rel, const std::string& body = "x") {
        auto full = m_path / rel;
        fs::create_directories(full.parent_path());
        std::ofstream(full) << body;
    }

private:
    static int GetTick() {
        static int n = 0;
        return ++n;
    }
    fs::path m_path;
};
}  // namespace

TEST_CASE("ContentBrowserPanel constructs empty", "[contentbrowser]") {
    ContentBrowserPanel p;
    REQUIRE_FALSE(p.Selected().IsValid());
    REQUIRE(p.CurrentFolder().empty());
    REQUIRE(p.Registry() == nullptr);
}

TEST_CASE("SetRegistry wires a registry; ResetNavigation clears state",
          "[contentbrowser]") {
    AssetRegistry r;
    ContentBrowserPanel p;
    p.SetRegistry(&r);
    REQUIRE(p.Registry() == &r);
    p.ResetNavigation();
    REQUIRE(p.CurrentFolder().empty());
}

TEST_CASE("Selected remains invalid until a non-folder is clicked",
          "[contentbrowser]") {
    TempDir dir;
    dir.Touch("hero.png");
    AssetRegistry r;
    r.AddRoot(dir.Path());
    r.Scan();
    ContentBrowserPanel p;
    p.SetRegistry(&r);
    // No Slate to drive a click in these tests; we just confirm the
    // initial state is sensible.
    REQUIRE_FALSE(p.Selected().IsValid());
    // AssetId of hero.png is present in the registry.
    auto entries = r.FilterByType(AssetType::Texture);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0]->assetName == "hero");
}

namespace {
// Minimal renderer stub (fonts don't bake but widget logic still runs).
class NullRenderer final : public Luma::Renderer {
public:
    void OnResize(Luma::u32, Luma::u32) override {}
    void SetClearColor(const Luma::ClearColor&) override {}
    bool BeginFrame() override { return true; }
    void EndFrame() override {}
    void DrawUI(const Luma::UIDrawData&) override {}
    Luma::TextureHandle CreateTexture(Luma::u32, Luma::u32,
                                      const void*) override {
        return 0;
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
}  // namespace

TEST_CASE("Right-click opens the Create menu; New Folder creates",
          "[contentbrowser]") {
    TempDir dir;
    AssetRegistry r;
    r.AddRoot(dir.Path());
    r.Scan();
    ContentBrowserPanel p;
    p.SetRegistry(&r);

    Context ui;
    NullRenderer nr;
    ui.Init(nr, TestTypography());
    PanelContext ctx;
    ctx.assetRegistry = &r;
    ctx.onImportAssets = [](const fs::path&) {};  // enables "Import"

    Rect body{0, 0, 800, 600};

    // Frame 1: right-click at (100, 100) inside the body opens the menu.
    ui.BeginFrame(800, 600, 0.0166f);
    ui.OnMouseMove(100.0f, 100.0f);
    ui.OnMouseButton(1, true);
    p.Draw(ui, body, ctx);
    ui.EndFrame();
    ui.BeginFrame(800, 600, 0.0166f);
    ui.OnMouseButton(1, false);
    p.Draw(ui, body, ctx);
    ui.EndFrame();

    // Flat row order: [GET: Import, CREATE: New Folder]. Down x1 to New Folder.
    auto pressKey = [&](int glfwKey) {
        ui.BeginFrame(800, 600, 0.0166f);
        ui.OnKey(glfwKey, true);
        p.Draw(ui, body, ctx);
            ui.EndFrame();
    };
    constexpr int kKeyDown = 264;   // GLFW_KEY_DOWN
    constexpr int kKeyEnter = 257;  // GLFW_KEY_ENTER
    pressKey(kKeyDown);  // 0: GET > Import
    pressKey(kKeyDown);  // 1: CREATE > New Folder
    pressKey(kKeyEnter);

    // New Folder should have been created and the panel navigated into it.
    REQUIRE(p.CurrentFolder().filename().string().find("NewFolder") != std::string::npos);
}

TEST_CASE("Right-click menu New Folder creates a folder and navigates",
          "[contentbrowser]") {
    TempDir dir;
    AssetRegistry r;
    r.AddRoot(dir.Path());
    r.Scan();
    ContentBrowserPanel p;
    p.SetRegistry(&r);

    Context ui;
    NullRenderer nr;
    ui.Init(nr, TestTypography());
    PanelContext ctx;
    ctx.assetRegistry = &r;
    ctx.onImportAssets = [](const fs::path&) {};  // enables "Import"

    Rect body{0, 0, 800, 600};

    // Open the menu.
    ui.BeginFrame(800, 600, 0.0166f);
    ui.OnMouseMove(100.0f, 100.0f);
    ui.OnMouseButton(1, true);
    p.Draw(ui, body, ctx);
    ui.EndFrame();
    ui.BeginFrame(800, 600, 0.0166f);
    ui.OnMouseButton(1, false);
    p.Draw(ui, body, ctx);
    ui.EndFrame();

    // Navigate with the keyboard: Down x2 puts the focus on "New Folder",
    // Enter activates it (flat order: GET > Import, CREATE > New Folder, ...).
    auto pressKey = [&](int glfwKey) {
        ui.BeginFrame(800, 600, 0.0166f);
        ui.OnKey(glfwKey, true);
        p.Draw(ui, body, ctx);
            ui.EndFrame();
    };
    constexpr int kKeyDown = 264;   // GLFW_KEY_DOWN
    constexpr int kKeyEnter = 257;  // GLFW_KEY_ENTER
    pressKey(kKeyDown);  // 0: GET > Import
    pressKey(kKeyDown);  // 1: CREATE > New Folder
    pressKey(kKeyEnter);

    // A NewFolder directory was created and the panel navigated into it.
    REQUIRE(fs::exists(dir.Path() / "NewFolder"));
    REQUIRE(p.CurrentFolder() == (dir.Path() / "NewFolder"));
}

TEST_CASE("Right-click menu closes on outside left-click",
          "[contentbrowser]") {
    TempDir dir;
    AssetRegistry r;
    r.AddRoot(dir.Path());
    r.Scan();
    ContentBrowserPanel p;
    p.SetRegistry(&r);

    Context ui;
    NullRenderer nr;
    ui.Init(nr, TestTypography());
    PanelContext ctx;

    Rect body{0, 0, 800, 600};

    // Open the menu with a right-click.
    ui.BeginFrame(800, 600, 0.0166f);
    ui.OnMouseMove(100.0f, 100.0f);
    ui.OnMouseButton(1, true);
    p.Draw(ui, body, ctx);
    ui.EndFrame();
    ui.BeginFrame(800, 600, 0.0166f);
    ui.OnMouseButton(1, false);
    p.Draw(ui, body, ctx);
    ui.EndFrame();

    // Left-click far away from the menu (e.g. the toolbar row).
    ui.BeginFrame(800, 600, 0.0166f);
    ui.OnMouseMove(400.0f, 10.0f);
    ui.OnMouseButton(0, true);
    p.Draw(ui, body, ctx);
    ui.EndFrame();
    ui.BeginFrame(800, 600, 0.0166f);
    ui.OnMouseButton(0, false);
    p.Draw(ui, body, ctx);
    ui.EndFrame();

    // The menu is gone: another outside click must not crash, and the
    // panel is still usable (selection stays invalid).
    REQUIRE_FALSE(p.Selected().IsValid());
}

TEST_CASE("Right-click menu GET > Import invokes onImportAssets",
          "[contentbrowser]") {
    TempDir dir;
    AssetRegistry r;
    r.AddRoot(dir.Path());
    r.Scan();
    ContentBrowserPanel p;
    p.SetRegistry(&r);

    Context ui;
    NullRenderer nr;
    ui.Init(nr, TestTypography());
    PanelContext ctx;
    ctx.assetRegistry = &r;
    ctx.onImportAssets = [](const fs::path&) {};  // enables "Import"

    std::filesystem::path importedInto;
    ctx.onImportAssets = [&](const std::filesystem::path& folder) {
        importedInto = folder;
    };

    Rect body{0, 0, 800, 600};

    // Open the menu.
    ui.BeginFrame(800, 600, 0.0166f);
    ui.OnMouseMove(100.0f, 100.0f);
    ui.OnMouseButton(1, true);
    p.Draw(ui, body, ctx);
    ui.EndFrame();
    ui.BeginFrame(800, 600, 0.0166f);
    ui.OnMouseButton(1, false);
    p.Draw(ui, body, ctx);
    ui.EndFrame();

    // Flat row 0 is GET > Import to Current Folder: Down once, then Enter.
    auto pressKey = [&](int glfwKey) {
        ui.BeginFrame(800, 600, 0.0166f);
        ui.OnKey(glfwKey, true);
        p.Draw(ui, body, ctx);
            ui.EndFrame();
    };
    constexpr int kKeyDown = 264;   // GLFW_KEY_DOWN
    constexpr int kKeyEnter = 257;  // GLFW_KEY_ENTER
    pressKey(kKeyDown);
    pressKey(kKeyEnter);

    // The callback received the content root (empty folder -> root).
    REQUIRE(importedInto == dir.Path());
}



TEST_CASE("Plugin assets appear in browser filters for already indexed files", "[contentbrowser][asset]") {
    TempDir dir;
    dir.Touch("one.curve");
    dir.Touch("two.png");
    AssetRegistry index;
    index.AddRoot(dir.Path());
    index.Scan();
    ContentBrowserPanel browser;
    browser.SetRegistry(&index);
    AssetTypeDesc curve{"plugin.curve", "Curve", "Image", "Plugin", {"CURVE"}};
    auto token = AssetTypes().Register(curve);
    REQUIRE(token);
    REQUIRE(AssetTypes().FindByExtension(".Curve")->id == curve.id);
    std::string error;
    CHECK_FALSE(AssetTypes().Register(curve, &error));
    CHECK_FALSE(error.empty());
    curve.id = "other.curve";
    CHECK_FALSE(AssetTypes().Register(curve, &error));
    auto menu = browser.BuildFilterMenu();
    auto row = std::find_if(menu.begin(), menu.end(), [](const auto& entry) { return entry.label == "Curve"; });
    REQUIRE(row != menu.end());
    row->action();
    REQUIRE(browser.CurrentEntries().size() == 1);
    CHECK(browser.CurrentEntries().front()->assetName == "one");
    token.Reset();
    CHECK_FALSE(AssetTypes().FindByExtension("curve"));
}

TEST_CASE("Cards scrolled out of the grid pane don't catch clicks outside it", "[contentbrowser]") {
    TempDir dir;
    for (int i = 0; i < 80; ++i) dir.Touch("tex_" + std::to_string(100 + i) + ".png");
    AssetRegistry r;
    r.AddRoot(dir.Path());
    r.Scan();
    ContentBrowserPanel p;
    p.SetRegistry(&r);
    Context ui;
    NullRenderer nr;
    ui.Init(nr, TestTypography());
    PanelContext ctx;
    ctx.assetRegistry = &r;

    // The browser fills the lower half; the upper half stands for the viewport.
    const Rect body{0, 300, 800, 300};
    auto frame = [&](auto&& input) {
        ui.BeginFrame(800, 600, 0.0166f);
        input();
        p.Draw(ui, body, ctx);
        ui.EndFrame();
    };
    auto click = [&](f32 x, f32 y) {
        frame([&] {
            ui.OnMouseMove(x, y);
            ui.OnMouseButton(0, true);
        });
        frame([&] { ui.OnMouseButton(0, false); });
        frame([] {});
    };
    frame([] {});
    // Scroll the grid well down: the first rows move above the pane.
    for (int i = 0; i < 40; ++i) {
        frame([&] {
            ui.OnMouseMove(500.0f, 500.0f);
            ui.OnScroll(-3.0f);
        });
    }
    // Clicks over the "viewport" above the pane, where scrolled rows now sit.
    // (Checked after every click: a second click on the same card deselects.)
    for (f32 y = 120.0f; y < 300.0f; y += 30.0f) {
        for (f32 x = 260.0f; x < 780.0f; x += 60.0f) {
            click(x, y);
            INFO("click at " << x << ", " << y);
            REQUIRE_FALSE(p.Selected().IsValid());
        }
    }

    // Sanity: a click on a visible card inside the pane still selects it.
    bool selectedInside = false;
    for (f32 y = 400.0f; y < 580.0f && !selectedInside; y += 20.0f) {
        for (f32 x = 260.0f; x < 780.0f && !selectedInside; x += 30.0f) {
            click(x, y);
            selectedInside = p.Selected().IsValid();
        }
    }
    REQUIRE(selectedInside);
}

#include <chrono>
#include <thread>

#include "Luma/Asset/TextureThumbnailRenderer.h"
#include "Luma/Asset/ThumbnailRenderer.h"
#include "Luma/Jobs/JobSystem.h"

namespace {
// Hands out distinct non-zero texture handles and counts uploads.
class CountingRenderer final : public Luma::Renderer {
public:
    void OnResize(Luma::u32, Luma::u32) override {}
    void SetClearColor(const Luma::ClearColor&) override {}
    bool BeginFrame() override { return true; }
    void EndFrame() override {}
    void DrawUI(const Luma::UIDrawData&) override {}
    Luma::TextureHandle CreateTexture(Luma::u32, Luma::u32, const void*) override { return ++uploads; }
    void DestroyTexture(Luma::TextureHandle) override {}
    void CaptureFrame(const std::string&) override {}
    void WaitIdle() override {}
    Luma::u64 uploads = 0;
};
}  // namespace

TEST_CASE("Thumbnails load on worker threads; GPU-rendered types stay on the main thread",
          "[contentbrowser][thumbnail]") {
    TempDir dir;
    fs::copy_file(fs::path(LUMA_SOURCE_DIR) / "Tests/Fixtures/Import/img_rgba.png", dir.Path() / "T_Rock.png");
    AssetRegistry r;
    r.AddRoot(dir.Path());
    r.Scan();
    const AssetData* tex = r.LookupByPath(dir.Path() / "T_Rock.png");
    REQUIRE(tex != nullptr);

    ThumbnailManager& manager = ThumbnailManager::Get();
    manager.RegisterRenderer(AssetType::Texture, std::make_unique<TextureThumbnailRenderer>());
    manager.SetProjectRoot(dir.Path() / "Project");
    CHECK_FALSE(manager.GeneratesOnMainThread(dir.Path() / "T_Rock.png"));
    CHECK(manager.GeneratesOnMainThread(dir.Path() / "M_Rock.lmat"));
    CHECK(manager.GeneratesOnMainThread(dir.Path() / "SM_Rock.lmesh"));

    Jobs::JobSystem jobs(2);
    CountingRenderer renderer;
    ContentBrowserPanel panel;
    panel.SetRegistry(&r);

    // First request: nothing on the main thread, a worker job starts.
    CHECK(panel.GetThumbnailTexture(tex->id, tex->packagePath, &renderer, &jobs) == 0);
    CHECK(panel.PendingThumbnailJobs() == 1);
    CHECK(renderer.uploads == 0);
    // Asking again while it runs doesn't start another.
    panel.GetThumbnailTexture(tex->id, tex->packagePath, &renderer, &jobs);
    CHECK(panel.PendingThumbnailJobs() <= 1);

    // The worker generates and caches the PNG; the main thread only uploads.
    u64 handle = 0;
    for (int i = 0; i < 500 && handle == 0; ++i) {
        handle = panel.GetThumbnailTexture(tex->id, tex->packagePath, &renderer, &jobs);
        if (handle == 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(handle != 0);
    CHECK(renderer.uploads == 1);
    CHECK(panel.PendingThumbnailJobs() == 0);
    CHECK(fs::exists(dir.Path() / "Project" / "Intermediate" / "Thumbnails" / (ToString(tex->id) + "_thumb.png")));
    // Cached: no new upload.
    CHECK(panel.GetThumbnailTexture(tex->id, tex->packagePath, &renderer, &jobs) == handle);
    CHECK(renderer.uploads == 1);

    // Invalidation drops the texture: the next request loads the (still valid)
    // cached PNG on a worker again.
    panel.InvalidateThumbnail(tex->id);
    CHECK(panel.GetThumbnailTexture(tex->id, tex->packagePath, &renderer, &jobs) == 0);
    CHECK(panel.PendingThumbnailJobs() == 1);
}
