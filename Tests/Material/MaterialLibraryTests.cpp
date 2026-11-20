// MaterialLibrary + resolution tests: instance chains, overrides, cycles,
// missing parents / textures, in-memory edits, saving and versioning.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Core/Log.h"
#include "Luma/Core/LogBuffer.h"
#include "Luma/Material/MaterialInstanceSerializer.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Material/MaterialSerializer.h"

using namespace Luma::Material;
using Luma::AssetPathResolver;
using Luma::LogBuffer;
using Luma::LogEntry;
using Luma::LogLevel;
using Luma::f32;
using Luma::u64;
using Luma::usize;
namespace Log = Luma::Log;
using Material = Luma::Material::Material;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    fs::path root;
    AssetPathResolver paths;
    std::unique_ptr<MaterialLibrary> library;

    Fixture() {
        static int counter = 0;
        root = fs::temp_directory_path() / ("luma_matlib_" + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root / "Game" / "Materials");
        fs::create_directories(root / "Game" / "Textures");
        fs::create_directories(root / "Engine" / "Materials");
        paths.SetGameRoot(root / "Game");
        paths.SetEngineRoot(root / "Engine");
        library = std::make_unique<MaterialLibrary>(paths);
    }
    ~Fixture() { fs::remove_all(root); }

    void WriteMaterial(const std::string& name, const Material& m) {
        REQUIRE(MaterialSerializer::SaveToFile(m, root / "Game" / "Materials" / (name + ".lmat")));
    }
    void WriteInstance(const std::string& name, const MaterialInstance& mi) {
        REQUIRE(MaterialInstanceSerializer::SaveToFile(
            mi, root / "Game" / "Materials" / (name + ".lmatinst")));
    }
    void WriteTexture(const std::string& name) {
        std::ofstream(root / "Game" / "Textures" / (name + ".png")) << "png";
    }
};

MaterialInput Scalar(f32 v) {
    MaterialInput in = Material::DefaultInputs()[0];
    in.constant = {v, 0.0f, 0.0f, 0.0f};
    in.channel = TextureChannel::R;
    return in;
}

MaterialInput Color(f32 r, f32 g, f32 b) {
    MaterialInput in = Material::DefaultInputs()[0];
    in.constant = {r, g, b, 1.0f};
    return in;
}

// Log sink shared by the tests that check user-facing warnings.
LogBuffer& Warnings() {
    static std::shared_ptr<LogBuffer> buffer = [] {
        auto b = std::make_shared<LogBuffer>(4096);
        Log::AddSink(b);
        return b;
    }();
    return *buffer;
}

usize CountWarningsContaining(u64 after, const std::string& needle) {
    std::vector<LogEntry> entries;
    Warnings().Fetch(after, entries);
    usize n = 0;
    for (const LogEntry& e : entries) {
        if (e.level == LogLevel::Warning && e.userFacing &&
            e.message.find(needle) != std::string::npos) {
            ++n;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("Empty path resolves to the Default Material", "[material][library]") {
    Fixture f;
    const ResolvedMaterial& r = f.library->Resolve("");
    const Material def = Material::MakeDefault();
    CHECK(r.inputs[0].constant.x == def.inputs[0].constant.x);
    CHECK(r.inputs[0].constant.z == def.inputs[0].constant.z);
    CHECK(r.rootMaterial == std::string(kDefaultMaterialPath));
}

TEST_CASE("Grandchild overrides apply over its chain", "[material][library]") {
    Fixture f;
    Material root = Material::MakeDefault();
    root.Input(MaterialParameter::BaseColor) = Color(0.2f, 0.4f, 0.6f);
    root.Input(MaterialParameter::Roughness) = Scalar(0.9f);
    root.Input(MaterialParameter::Metallic) = Scalar(0.1f);
    f.WriteMaterial("M_Root", root);

    MaterialInstance child;
    child.parent = "/Game/Materials/M_Root";
    child.Override(MaterialParameter::Metallic, Scalar(1.0f));
    child.twoSided = true;
    f.WriteInstance("MI_Child", child);

    MaterialInstance grand;
    grand.parent = "/Game/Materials/MI_Child";
    grand.Override(MaterialParameter::Roughness, Scalar(0.25f));
    f.WriteInstance("MI_Grand", grand);

    const ResolvedMaterial& r = f.library->Resolve("/Game/Materials/MI_Grand");
    CHECK(r.inputs[static_cast<usize>(MaterialParameter::Roughness)].constant.x == 0.25f);
    CHECK(r.inputs[static_cast<usize>(MaterialParameter::Metallic)].constant.x == 1.0f);
    CHECK(r.inputs[static_cast<usize>(MaterialParameter::BaseColor)].constant.y == 0.4f);
    CHECK(r.settings.twoSided);  // inherited from the child override
    CHECK(r.rootMaterial == "/Game/Materials/M_Root");
}

TEST_CASE("A cycle on disk resolves to Default without hanging", "[material][library]") {
    Fixture f;
    MaterialInstance a;
    a.parent = "/Game/Materials/MI_B";
    a.Override(MaterialParameter::Roughness, Scalar(0.1f));
    f.WriteInstance("MI_A", a);
    MaterialInstance b;
    b.parent = "/Game/Materials/MI_A";
    f.WriteInstance("MI_B", b);

    const u64 mark = Warnings().LatestSequence();
    const ResolvedMaterial& r = f.library->Resolve("/Game/Materials/MI_A");
    CHECK(r.rootMaterial == std::string(kDefaultMaterialPath));
    CHECK(r.inputs[static_cast<usize>(MaterialParameter::Roughness)].constant.x == 0.5f);
    CHECK(CountWarningsContaining(mark, "MI_A") >= 1);
}

TEST_CASE("Missing parent resolves to Default with one warning", "[material][library]") {
    Fixture f;
    MaterialInstance orphan;
    orphan.parent = "/Game/Materials/M_Gone";
    f.WriteInstance("MI_Orphan", orphan);
    const u64 mark = Warnings().LatestSequence();
    f.library->Resolve("/Game/Materials/MI_Orphan");
    f.library->Resolve("/Game/Materials/MI_Orphan");
    CHECK(CountWarningsContaining(mark, "M_Gone") == 1);  // warned once
    CHECK(f.library->Resolve("/Game/Materials/MI_Orphan").rootMaterial ==
          std::string(kDefaultMaterialPath));
}

TEST_CASE("CanSetParent refuses cycles, self and unknown parents", "[material][library]") {
    Fixture f;
    f.WriteMaterial("M_Root", Material::MakeDefault());
    MaterialInstance a;
    a.parent = "/Game/Materials/M_Root";
    f.WriteInstance("MI_A", a);
    MaterialInstance b;
    b.parent = "/Game/Materials/MI_A";
    f.WriteInstance("MI_B", b);

    CHECK(f.library->CanSetParent("/Game/Materials/MI_B", "/Game/Materials/M_Root"));
    CHECK_FALSE(f.library->CanSetParent("/Game/Materials/MI_A", "/Game/Materials/MI_B"));
    CHECK_FALSE(f.library->CanSetParent("/Game/Materials/MI_A", "/Game/Materials/MI_A"));
    CHECK_FALSE(f.library->CanSetParent("/Game/Materials/MI_A", "/Game/Materials/M_Missing"));
}

TEST_CASE("Edits bump the version, mark dirty, and Save clears it", "[material][library]") {
    Fixture f;
    f.WriteMaterial("M_Edit", Material::MakeDefault());
    const std::string path = "/Game/Materials/M_Edit";
    const u64 v0 = f.library->Resolve(path).version;
    CHECK_FALSE(f.library->IsDirty(path));

    Material m = *f.library->GetMaterial(path);
    m.Input(MaterialParameter::Roughness) = Scalar(0.05f);
    f.library->SetMaterial(path, m);
    CHECK(f.library->IsDirty(path));
    CHECK(f.library->Resolve(path).version > v0);
    CHECK(f.library->Resolve(path).inputs[static_cast<usize>(MaterialParameter::Roughness)]
              .constant.x == 0.05f);
    REQUIRE(f.library->DirtyAssets() == std::vector<std::string>{path});

    REQUIRE(f.library->Save(path));
    CHECK_FALSE(f.library->IsDirty(path));
    Material onDisk;
    REQUIRE(MaterialSerializer::LoadFromFile(onDisk, f.root / "Game" / "Materials" / "M_Edit.lmat"));
    CHECK(onDisk.Input(MaterialParameter::Roughness).constant.x == 0.05f);
}

TEST_CASE("Editing a parent re-resolves its instances", "[material][library]") {
    Fixture f;
    f.WriteMaterial("M_P", Material::MakeDefault());
    MaterialInstance mi;
    mi.parent = "/Game/Materials/M_P";
    f.WriteInstance("MI_C", mi);
    const u64 v0 = f.library->Resolve("/Game/Materials/MI_C").version;
    Material p = *f.library->GetMaterial("/Game/Materials/M_P");
    p.Input(MaterialParameter::BaseColor) = Color(1.0f, 0.0f, 0.0f);
    f.library->SetMaterial("/Game/Materials/M_P", p);
    const ResolvedMaterial& r = f.library->Resolve("/Game/Materials/MI_C");
    CHECK(r.version > v0);
    CHECK(r.inputs[0].constant.x == 1.0f);
}

TEST_CASE("A deleted texture falls back to the constant with one warning",
          "[material][library]") {
    Fixture f;
    f.WriteTexture("T_Wood");
    Material m = Material::MakeDefault();
    m.Input(MaterialParameter::BaseColor).texture = "/Game/Textures/T_Wood";
    f.WriteMaterial("M_Tex", m);
    CHECK(f.library->Resolve("/Game/Materials/M_Tex").inputs[0].texture ==
          "/Game/Textures/T_Wood");

    fs::remove(f.root / "Game" / "Textures" / "T_Wood.png");
    f.library->Reload("/Game/Materials/M_Tex");
    const u64 mark = Warnings().LatestSequence();
    CHECK(f.library->Resolve("/Game/Materials/M_Tex").inputs[0].texture.empty());
    f.library->Resolve("/Game/Materials/M_Tex");
    CHECK(CountWarningsContaining(mark, "T_Wood") == 1);
}

TEST_CASE("In-memory instances get unique paths and are dirty until saved",
          "[material][library]") {
    Fixture f;
    MaterialInstance mi;
    mi.parent = std::string(kDefaultMaterialPath);
    mi.Override(MaterialParameter::BaseColor, Color(1.0f, 0.0f, 0.0f));
    f.WriteInstance("MI_Red", mi);  // occupies the desired name on disk
    const std::string path = f.library->CreateInMemoryInstance("/Game/Materials/MI_Red", mi);
    CHECK(path != "/Game/Materials/MI_Red");
    CHECK(f.library->IsDirty(path));
    CHECK_FALSE(fs::exists(f.root / "Game" / "Materials" / "MI_Red_1.lmatinst"));
    CHECK(f.library->Resolve(path).inputs[0].constant.x == 1.0f);
    REQUIRE(f.library->Save(path));
    CHECK(fs::exists(*f.paths.ToFile(path, {"lmatinst"})));
}

TEST_CASE("Shipped Default Material content matches MakeDefault", "[material][library]") {
    const fs::path file = fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine" / "Materials" /
                          "DefaultMaterial.lmat";
    REQUIRE(fs::exists(file));
    Material loaded;
    std::string err;
    REQUIRE(MaterialSerializer::LoadFromFile(loaded, file, &err));
    const Material def = Material::MakeDefault();
    for (usize i = 0; i < kMaterialParameterCount; ++i) {
        CHECK(loaded.inputs[i].constant.x == def.inputs[i].constant.x);
        CHECK(loaded.inputs[i].constant.y == def.inputs[i].constant.y);
        CHECK(loaded.inputs[i].constant.z == def.inputs[i].constant.z);
        CHECK(loaded.inputs[i].channel == def.inputs[i].channel);
        CHECK(loaded.inputs[i].texture.empty());
    }
    CHECK(loaded.settings.blendMode == def.settings.blendMode);
    CHECK(loaded.settings.opacityMaskClip == def.settings.opacityMaskClip);
}
