#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Asset/Import/ImportSession.h"
#include "Luma/Asset/Import/TranslatorRegistry.h"
#include "Luma/Material/MaterialLibrary.h"

using Catch::Approx;
using namespace Luma;
using namespace Luma::Import;

namespace {

std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(LUMA_SOURCE_DIR) / "Tests" / "Fixtures" / "Import" / name;
}

struct Project {
    std::filesystem::path root;
    std::filesystem::path assets;
    AssetPathResolver paths;
    Material::MaterialLibrary materials;

    explicit Project(const char* name)
        : root(std::filesystem::temp_directory_path() / name),
          assets(root / "Assets"),
          materials(paths) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(assets);
        paths.SetGameRoot(assets);
    }

    ~Project() { std::filesystem::remove_all(root); }
};

int FileNodes(const ImportScene& scene) {
    int count = 0;
    for (const ImportNode& node : scene.nodes) {
        if (node.kind == NodeKind::File) {
            ++count;
        }
    }
    return count;
}

bool WaitUntilStopped(ImportSession& session) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!session.Running()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool HasTempFile(const std::filesystem::path& root) {
    if (!std::filesystem::exists(root)) {
        return false;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().string().find(".tmp") != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("Translator registry finds model, image, and audio", "[import][session]") {
    const TranslatorRegistry registry = TranslatorRegistry::CreateDefault();
    REQUIRE(registry.Find(Fixture("robot.glb")) != nullptr);
    REQUIRE(registry.Find(Fixture("img_rgba.png")) != nullptr);
    REQUIRE(registry.Find(Fixture("tone_mono.wav")) != nullptr);
    REQUIRE(registry.Find(Fixture("robot.glb"))->CanTranslate(Fixture("robot.glb")));
    const std::vector<std::string> extensions = registry.SupportedExtensions();
    REQUIRE(std::find(extensions.begin(), extensions.end(), ".glb") != extensions.end());
    REQUIRE(std::find(extensions.begin(), extensions.end(), ".png") != extensions.end());
    REQUIRE(std::find(extensions.begin(), extensions.end(), ".wav") != extensions.end());
}

TEST_CASE("Import session translates, imports, reimports, and detects source edits", "[import][session]") {
    Project project("luma-session-import");
    const std::filesystem::path glb = project.root / "robot.glb";
    std::filesystem::copy_file(Fixture("robot.glb"), glb);

    ImportSession session;
    std::string error;
    REQUIRE(session.Translate({glb, Fixture("tone_mono.wav"), Fixture("img_rgba.png")}, &error));
    REQUIRE(FileNodes(session.Scene()) == 3);

    session.Settings().Meshes().Set("uniformScale", 2.0);
    const ImportPlan plan = session.Plan(project.assets);
    REQUIRE(plan.requests.size() > 3);

    FactoryContext context{session.Scene(), session.Settings(), &project.materials, project.paths,
                           [] { return false; }};
    session.Start(plan, context);
    REQUIRE(WaitUntilStopped(session));
    REQUIRE(session.Progress() == Approx(1.0f));

    const std::optional<ImportSummary> summary = session.TakeResult();
    REQUIRE(summary);
    REQUIRE(summary->failed == 0);
    REQUIRE(summary->created == plan.requests.size());
    REQUIRE(summary->files.size() == summary->created);
    REQUIRE(std::filesystem::exists(project.assets / "SM_Body.lmesh"));
    REQUIRE_FALSE(SourceChanged(project.assets / "SM_Body.lmesh"));

    const std::unique_ptr<ImportSession> reimport = ImportSession::ForReimport(project.assets / "SM_Body.lmesh", &error);
    REQUIRE(reimport);
    REQUIRE(reimport->Settings().Meshes().GetFloat("uniformScale") == Approx(2.0));

    {
        std::ofstream append(glb, std::ios::binary | std::ios::app);
        append << '\n';
    }
    REQUIRE(SourceChanged(project.assets / "SM_Body.lmesh"));
}

TEST_CASE("Cancelling a multi-file import stops early", "[import][session]") {
    Project project("luma-session-cancel");
    std::vector<std::filesystem::path> copies;
    copies.reserve(50);
    for (int i = 0; i < 50; ++i) {
        const std::filesystem::path copy = project.root / ("img_" + std::to_string(i) + ".png");
        std::filesystem::copy_file(Fixture("img_rgba.png"), copy);
        copies.push_back(copy);
    }

    ImportSession session;
    std::string error;
    REQUIRE(session.Translate(copies, &error));
    const ImportPlan plan = session.Plan(project.assets);
    REQUIRE(plan.requests.size() == 50);

    std::atomic<int> checks{0};
    FactoryContext context{session.Scene(), session.Settings(), &project.materials, project.paths, [&] {
                               if (checks.fetch_add(1) == 0) {
                                   return false;
                               }
                               session.Cancel();
                               return true;
                           }};
    session.Start(plan, context);
    REQUIRE(WaitUntilStopped(session));
    const std::optional<ImportSummary> summary = session.TakeResult();
    REQUIRE(summary);
    REQUIRE(summary->created < 50);
    bool reported = false;
    for (const std::string& message : summary->messages) {
        if (message.find("cancel") != std::string::npos) {
            reported = true;
        }
    }
    REQUIRE(reported);
    REQUIRE_FALSE(HasTempFile(project.assets));
}
