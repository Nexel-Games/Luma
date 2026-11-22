#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "Luma/Core/Version.h"
#include "Luma/Project/Project.h"
#include "Luma/Serialization/Json.h"

namespace fs = std::filesystem;
using Luma::Project;
using Luma::SerialValue;

namespace {

struct TempDir {
    fs::path root;
    explicit TempDir(const char* name) : root(fs::temp_directory_path() / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }
    ~TempDir() {
        std::error_code ec;
        fs::permissions(root, fs::perms::all, fs::perm_options::add, ec);
        fs::remove_all(root, ec);
    }
};

void WriteFile(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("New projects are written as JSON file version 2", "[project][projectfile]") {
    TempDir dir("luma_projfile_create");
    Luma::ProjectDesc desc;
    desc.name = "Json Game";
    desc.parentDirectory = dir.root;
    desc.gameTemplate = Luma::GameTemplate::TopDown;
    auto project = Project::Create(desc);
    REQUIRE(project.has_value());
    const auto doc = Luma::ParseJson(ReadFile(project->ProjectFile()));
    REQUIRE(doc.has_value());
    REQUIRE(doc->Find("fileVersion")->AsInt() == Project::kFileVersion);
    REQUIRE(doc->Find("name")->AsString() == "Json Game");
    REQUIRE(doc->Find("template")->AsString() == "TopDown");
    REQUIRE(doc->Find("engineVersion")->AsString() == Luma::EngineVersion().ToString());
    REQUIRE(doc->Find("startupScene")->AsString() == "Assets/Scenes/Main.lscene");
    REQUIRE(project->Template() == Luma::GameTemplate::TopDown);
}

TEST_CASE("INI project files load and are rewritten as JSON on save", "[project][projectfile]") {
    TempDir dir("luma_projfile_ini");
    const fs::path file = dir.root / "Old.luma";
    WriteFile(file, "# Luma project descriptor\n[Project]\nname = Old Game\n"
                    "engine_version = 0.0.9\ntemplate = FirstPerson\n"
                    "startup_scene = Assets/Scenes/Level.lscene\n");
    auto project = Project::Load(file);
    REQUIRE(project.has_value());
    REQUIRE(project->Name() == "Old Game");
    REQUIRE(project->EngineVersion() == "0.0.9");
    REQUIRE(project->Template() == Luma::GameTemplate::FirstPerson);
    REQUIRE(project->StartupScene() == "Assets/Scenes/Level.lscene");

    std::string error;
    REQUIRE(project->Save(&error));
    const auto doc = Luma::ParseJson(ReadFile(file));
    REQUIRE(doc.has_value());
    REQUIRE(doc->Find("fileVersion")->AsInt() == 2);
    auto reloaded = Project::Load(file);
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->Name() == "Old Game");
    REQUIRE(reloaded->StartupScene() == "Assets/Scenes/Level.lscene");
    REQUIRE(reloaded->Template() == Luma::GameTemplate::FirstPerson);
}

TEST_CASE("Version-1 JSON descriptors migrate their keys", "[project][projectfile]") {
    TempDir dir("luma_projfile_v1json");
    const fs::path file = dir.root / "Legacy.luma";
    WriteFile(file, R"({"projectName": "Legacy", "engineVersion": "0.1.0",
                        "defaultScene": "Main.lscene", "custom": {"keep": true}})");
    auto project = Project::Load(file);
    REQUIRE(project.has_value());
    REQUIRE(project->Name() == "Legacy");
    REQUIRE(project->StartupScene() == "Main.lscene");
    REQUIRE(project->Save());
    const auto doc = Luma::ParseJson(ReadFile(file));
    REQUIRE(doc->Find("projectName") == nullptr);
    REQUIRE(doc->Find("defaultScene") == nullptr);
    REQUIRE(doc->Find("name")->AsString() == "Legacy");
    REQUIRE(doc->Find("custom")->Find("keep")->AsBool());
}

TEST_CASE("Unknown keys survive load and save in order", "[project][projectfile]") {
    TempDir dir("luma_projfile_unknown");
    const fs::path file = dir.root / "Keep.luma";
    const std::string original =
        R"({"fileVersion": 2, "name": "Keep", "plugins": {"schema": 1, "enabled": ["A.B"]},)"
        R"( "engineVersion": "0.1.0", "template": "Empty", "startupScene": "Main.lscene",)"
        R"( "zzz": [1, "two", null]})";
    WriteFile(file, original);
    auto project = Project::Load(file);
    REQUIRE(project.has_value());
    REQUIRE(project->Find("plugins") != nullptr);
    REQUIRE(project->Save());
    REQUIRE(ReadFile(file) == Luma::WriteJson(*Luma::ParseJson(original), true));
}

TEST_CASE("Project sections can be set and saved; reserved keys are protected",
          "[project][projectfile]") {
    TempDir dir("luma_projfile_set");
    Luma::ProjectDesc desc;
    desc.name = "Sections";
    desc.parentDirectory = dir.root;
    auto project = Project::Create(desc);
    REQUIRE(project.has_value());

    SerialValue plugins = SerialValue::MakeObject();
    plugins["schema"] = 1;
    std::string error;
    REQUIRE(project->Set("plugins", plugins, &error));
    REQUIRE_FALSE(project->Set("name", SerialValue{"Hacked"}, &error));
    REQUIRE(error.find("name") != std::string::npos);
    REQUIRE_FALSE(project->Set("fileVersion", SerialValue{9}, &error));
    REQUIRE(project->Save(&error));

    auto reloaded = Project::Load(project->ProjectFile());
    REQUIRE(reloaded->Find("plugins")->Find("schema")->AsInt() == 1);
    REQUIRE(reloaded->Name() == "Sections");

    project->SetStartupScene("Assets/Scenes/Other.lscene");
    REQUIRE(project->Save());
    REQUIRE(Project::Load(project->ProjectFile())->StartupScene() == "Assets/Scenes/Other.lscene");
}

TEST_CASE("Bad project files are rejected with the reason", "[project][projectfile]") {
    TempDir dir("luma_projfile_bad");
    std::string error;

    const fs::path newer = dir.root / "Newer.luma";
    WriteFile(newer, R"({"fileVersion": 99, "name": "Future"})");
    REQUIRE_FALSE(Project::Load(newer, &error).has_value());
    REQUIRE(error.find("newer") != std::string::npos);

    const fs::path broken = dir.root / "Broken.luma";
    WriteFile(broken, R"({"fileVersion": 2, "name": )");
    REQUIRE_FALSE(Project::Load(broken, &error).has_value());
    REQUIRE(error.find("Broken.luma") != std::string::npos);

    const fs::path wrongType = dir.root / "Wrong.luma";
    WriteFile(wrongType, R"({"fileVersion": 2, "name": 42})");
    REQUIRE_FALSE(Project::Load(wrongType, &error).has_value());
    REQUIRE(error.find("name") != std::string::npos);
}

TEST_CASE("A read-only project file reports that it can't be saved", "[project][projectfile]") {
    TempDir dir("luma_projfile_ro");
    Luma::ProjectDesc desc;
    desc.name = "ReadOnly";
    desc.parentDirectory = dir.root;
    auto project = Project::Create(desc);
    REQUIRE(project.has_value());
    REQUIRE(project->IsWritable());
    // (Windows maps "no write bits at all" to the read-only attribute.)
    constexpr fs::perms kWrite = fs::perms::owner_write | fs::perms::group_write |
                                 fs::perms::others_write;
    fs::permissions(project->ProjectFile(), kWrite, fs::perm_options::remove);
    REQUIRE_FALSE(project->IsWritable());
    std::string error;
    REQUIRE_FALSE(project->Save(&error));
    REQUIRE(error.find("read-only") != std::string::npos);
    fs::permissions(project->ProjectFile(), kWrite, fs::perm_options::add);
}
