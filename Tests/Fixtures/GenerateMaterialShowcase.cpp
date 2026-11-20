// Generates Tests/Fixtures/MaterialShowcase: a small project exercising every
// material feature (sRGB color maps, OpenGL normal maps, data maps, alpha
// masking, emissive, instances). Hidden test - run on demand:
//
//   LumaTests.exe "[.fixture]"
//
// Everything is written through the engine's own serializers so the fixture
// always matches the current file formats.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include "Luma/Material/MaterialInstanceSerializer.h"
#include "Luma/Material/MaterialSerializer.h"
#include "Luma/Scene/SceneSerializer.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#include "stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace fs = std::filesystem;
using namespace Luma;
using Luma::Material::BlendMode;
using Luma::Material::MaterialInput;
using Luma::Material::MaterialInstance;
using Luma::Material::MaterialParameter;
using Luma::Material::TextureChannel;
using MaterialAsset = Luma::Material::Material;

namespace {

constexpr int kSize = 256;

u8 ToByte(f32 v) { return static_cast<u8>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

void WritePng(const fs::path& file, int channels, const std::vector<u8>& px) {
    REQUIRE(stbi_write_png(file.string().c_str(), kSize, kSize, channels, px.data(),
                           kSize * channels) != 0);
}

// Checkerboard, 8x8 cells, sRGB color.
void WriteChecker(const fs::path& file) {
    std::vector<u8> px(kSize * kSize * 3);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const bool light = ((x / 32) + (y / 32)) % 2 == 0;
            u8* p = &px[(y * kSize + x) * 3];
            p[0] = light ? 214 : 52;
            p[1] = light ? 208 : 58;
            p[2] = light ? 196 : 72;
        }
    }
    WritePng(file, 3, px);
}

// A 4x4 grid of hemispherical bumps as an OpenGL-style (+Y = image up)
// tangent-space normal map.
void WriteBumpNormals(const fs::path& file) {
    auto height = [](f32 u, f32 v) {
        const f32 cu = std::fmod(u * 4.0f, 1.0f) - 0.5f;
        const f32 cv = std::fmod(v * 4.0f, 1.0f) - 0.5f;
        const f32 r2 = (cu * cu + cv * cv) / 0.16f;
        return r2 < 1.0f ? std::sqrt(1.0f - r2) * 0.02f : 0.0f;
    };
    std::vector<u8> px(kSize * kSize * 3);
    const f32 d = 1.0f / kSize;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const f32 u = (x + 0.5f) * d, v = (y + 0.5f) * d;
            const f32 dhdu = (height(u + d, v) - height(u - d, v)) / (2.0f * d);
            const f32 dhdv = (height(u, v + d) - height(u, v - d)) / (2.0f * d);
            // v grows down the image; +Y of the map points image-up.
            f32 nx = -dhdu, ny = dhdv, nz = 1.0f;
            const f32 len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len, ny /= len, nz /= len;
            u8* p = &px[(y * kSize + x) * 3];
            p[0] = ToByte(nx * 0.5f + 0.5f);
            p[1] = ToByte(ny * 0.5f + 0.5f);
            p[2] = ToByte(nz * 0.5f + 0.5f);
        }
    }
    WritePng(file, 3, px);
}

// Left-to-right roughness ramp (grayscale data).
void WriteRoughnessGradient(const fs::path& file) {
    std::vector<u8> px(kSize * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) px[y * kSize + x] = static_cast<u8>(x);
    }
    WritePng(file, 1, px);
}

// Leaf silhouettes: green color with alpha 255 inside the leaves, 0 outside.
void WriteLeaves(const fs::path& file) {
    std::vector<u8> px(kSize * kSize * 4);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const f32 u = std::fmod(x / 64.0f, 1.0f) - 0.5f;
            const f32 v = std::fmod(y / 64.0f, 1.0f) - 0.5f;
            // A leaf: an ellipse rotated 45 degrees.
            const f32 a = (u + v) * 0.7071f, b = (u - v) * 0.7071f;
            const bool inside = (a * a) / 0.16f + (b * b) / 0.03f < 1.0f;
            const f32 vein = std::fabs(b) < 0.012f ? 0.6f : 1.0f;
            u8* p = &px[(y * kSize + x) * 4];
            p[0] = ToByte(0.20f * vein);
            p[1] = ToByte(0.55f * vein);
            p[2] = ToByte(0.12f * vein);
            p[3] = inside ? 255 : 0;
        }
    }
    WritePng(file, 4, px);
}

// Diagonal warm stripes on black (emissive color).
void WriteStripes(const fs::path& file) {
    std::vector<u8> px(kSize * kSize * 3);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const bool on = ((x + y) / 24) % 2 == 0;
            u8* p = &px[(y * kSize + x) * 3];
            p[0] = on ? 255 : 0;
            p[1] = on ? 140 : 0;
            p[2] = on ? 40 : 0;
        }
    }
    WritePng(file, 3, px);
}

// Packed ORM data map: R = ambient occlusion (dark cell corners),
// G = roughness (top-to-bottom ramp), B = metallic (right half metal).
void WriteORM(const fs::path& file) {
    std::vector<u8> px(kSize * kSize * 3);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const f32 cu = std::fmod(x / 64.0f, 1.0f) - 0.5f;
            const f32 cv = std::fmod(y / 64.0f, 1.0f) - 0.5f;
            const f32 ao = 1.0f - std::clamp((cu * cu + cv * cv) * 3.0f - 0.2f, 0.0f, 0.8f);
            u8* p = &px[(y * kSize + x) * 3];
            p[0] = ToByte(ao);
            p[1] = ToByte(0.1f + 0.8f * (y / static_cast<f32>(kSize - 1)));
            p[2] = x >= kSize / 2 ? 255 : 0;
        }
    }
    WritePng(file, 3, px);
}

MaterialInput Texture(MaterialParameter p, const char* path,
                      TextureChannel channel = TextureChannel::RGB) {
    MaterialInput in = MaterialAsset::DefaultInputs()[static_cast<usize>(p)];
    in.texture = path;
    in.channel = channel;
    return in;
}

MaterialInput Constant(MaterialParameter p, Math::Vec4 value) {
    MaterialInput in = MaterialAsset::DefaultInputs()[static_cast<usize>(p)];
    in.constant = value;
    return in;
}

void Set(MaterialAsset& m, MaterialParameter p, MaterialInput in) {
    m.inputs[static_cast<usize>(p)] = std::move(in);
}

void SaveMaterial(const fs::path& dir, MaterialAsset m) {
    REQUIRE(Luma::Material::MaterialSerializer::SaveToFile(m, dir / (m.name + ".lmat")));
}

Entity Actor(Scene& scene, const char* name, MeshPrimitive shape, Math::Vec3 position,
             Math::Vec3 scale, std::vector<std::string> materials) {
    const Entity e = scene.CreateEntity(name);
    auto& tf = scene.Registry().get<TransformComponent>(e);
    tf.position = position;
    tf.scale = scale;
    auto& mr = scene.Registry().emplace<MeshRendererComponent>(e);
    mr.primitive = shape;
    mr.materials = std::move(materials);
    return e;
}

}  // namespace

TEST_CASE("Generate the MaterialShowcase fixture", "[.fixture]") {
    const fs::path root = fs::path(LUMA_SOURCE_DIR) / "Tests" / "Fixtures" / "MaterialShowcase";
    const fs::path textures = root / "Assets" / "Textures";
    const fs::path materials = root / "Assets" / "Materials";
    const fs::path scenes = root / "Assets" / "Scenes";
    fs::remove_all(root);
    fs::create_directories(textures);
    fs::create_directories(materials);
    fs::create_directories(scenes);

    {
        std::ofstream project(root / "MaterialShowcase.luma");
        project << "# Luma project descriptor\n[Project]\nname = MaterialShowcase\n"
                   "engine_version = 0.1.0\ntemplate = Empty\n"
                   "startup_scene = Assets/Scenes/Main.lscene\n";
    }

    WriteChecker(textures / "T_Checker_diff.png");
    WriteBumpNormals(textures / "T_Bumps_nor_gl.png");
    WriteRoughnessGradient(textures / "T_Gradient_rough.png");
    WriteLeaves(textures / "T_Leaves_diff.png");
    WriteStripes(textures / "T_Stripes_emissive.png");
    WriteORM(textures / "T_Copper_ORM.png");

    {  // Tiled checker floor.
        MaterialAsset m = MaterialAsset::MakeDefault();
        m.name = "M_Checker";
        MaterialInput base = Texture(MaterialParameter::BaseColor, "/Game/Textures/T_Checker_diff");
        base.uvTiling = {8.0f, 8.0f};
        Set(m, MaterialParameter::BaseColor, base);
        Set(m, MaterialParameter::Roughness, Constant(MaterialParameter::Roughness, {0.7f, 0.7f, 0.7f, 0.7f}));
        SaveMaterial(materials, m);
    }
    {  // Glossy bumps.
        MaterialAsset m = MaterialAsset::MakeDefault();
        m.name = "M_Bumps";
        Set(m, MaterialParameter::BaseColor, Constant(MaterialParameter::BaseColor, {0.55f, 0.08f, 0.06f, 1.0f}));
        MaterialInput normal = Texture(MaterialParameter::Normal, "/Game/Textures/T_Bumps_nor_gl");
        normal.normalStrength = 1.0f;
        Set(m, MaterialParameter::Normal, normal);
        Set(m, MaterialParameter::Roughness, Constant(MaterialParameter::Roughness, {0.25f, 0.25f, 0.25f, 0.25f}));
        SaveMaterial(materials, m);
    }
    {  // Gold with a roughness ramp.
        MaterialAsset m = MaterialAsset::MakeDefault();
        m.name = "M_GoldRamp";
        Set(m, MaterialParameter::BaseColor, Constant(MaterialParameter::BaseColor, {1.0f, 0.77f, 0.34f, 1.0f}));
        Set(m, MaterialParameter::Metallic, Constant(MaterialParameter::Metallic, {1.0f, 1.0f, 1.0f, 1.0f}));
        Set(m, MaterialParameter::Roughness,
            Texture(MaterialParameter::Roughness, "/Game/Textures/T_Gradient_rough", TextureChannel::R));
        SaveMaterial(materials, m);
    }
    {  // Alpha-masked, two-sided leaves.
        MaterialAsset m = MaterialAsset::MakeDefault();
        m.name = "M_Leaves";
        m.settings.blendMode = BlendMode::Masked;
        m.settings.twoSided = true;
        Set(m, MaterialParameter::BaseColor, Texture(MaterialParameter::BaseColor, "/Game/Textures/T_Leaves_diff"));
        Set(m, MaterialParameter::OpacityMask,
            Texture(MaterialParameter::OpacityMask, "/Game/Textures/T_Leaves_diff", TextureChannel::A));
        Set(m, MaterialParameter::Roughness, Constant(MaterialParameter::Roughness, {0.6f, 0.6f, 0.6f, 0.6f}));
        SaveMaterial(materials, m);
    }
    {  // Emissive stripes.
        MaterialAsset m = MaterialAsset::MakeDefault();
        m.name = "M_Emissive";
        Set(m, MaterialParameter::BaseColor, Constant(MaterialParameter::BaseColor, {0.05f, 0.05f, 0.05f, 1.0f}));
        MaterialInput emissive = Texture(MaterialParameter::EmissiveColor, "/Game/Textures/T_Stripes_emissive");
        emissive.emissiveIntensity = 6.0f;
        Set(m, MaterialParameter::EmissiveColor, emissive);
        SaveMaterial(materials, m);
    }
    {  // Unlit: flat color, no lighting.
        MaterialAsset m = MaterialAsset::MakeDefault();
        m.name = "M_Unlit";
        m.settings.shadingModel = Luma::Material::ShadingModel::Unlit;
        Set(m, MaterialParameter::BaseColor, Constant(MaterialParameter::BaseColor, {0.95f, 0.45f, 0.1f, 1.0f}));
        SaveMaterial(materials, m);
    }
    {  // Copper with a packed occlusion/roughness/metallic map.
        MaterialAsset m = MaterialAsset::MakeDefault();
        m.name = "M_CopperORM";
        Set(m, MaterialParameter::BaseColor, Constant(MaterialParameter::BaseColor, {0.95f, 0.64f, 0.54f, 1.0f}));
        Set(m, MaterialParameter::AmbientOcclusion,
            Texture(MaterialParameter::AmbientOcclusion, "/Game/Textures/T_Copper_ORM", TextureChannel::R));
        Set(m, MaterialParameter::Roughness,
            Texture(MaterialParameter::Roughness, "/Game/Textures/T_Copper_ORM", TextureChannel::G));
        Set(m, MaterialParameter::Metallic,
            Texture(MaterialParameter::Metallic, "/Game/Textures/T_Copper_ORM", TextureChannel::B));
        SaveMaterial(materials, m);
    }
    {  // Instance: a blue-tinted checker.
        MaterialInstance mi;
        mi.name = "MI_Checker_Blue";
        mi.parent = "/Game/Materials/M_Checker";
        MaterialInput base = Texture(MaterialParameter::BaseColor, "/Game/Textures/T_Checker_diff");
        base.tint = {0.35f, 0.55f, 1.0f, 1.0f};
        base.uvTiling = {2.0f, 2.0f};
        mi.Override(MaterialParameter::BaseColor, base);
        REQUIRE(Luma::Material::MaterialInstanceSerializer::SaveToFile(
            mi, materials / "MI_Checker_Blue.lmatinst"));
    }

    Scene scene;
    {
        const Entity env = scene.CreateEntity("Environment");
        scene.Registry().emplace<EnvironmentComponent>(env);
        const Entity sun = scene.CreateEntity("Sun");
        auto& tf = scene.Registry().get<TransformComponent>(sun);
        tf.position = {0.0f, 10.0f, 0.0f};
        tf.rotationEuler = {40.0f, 30.0f, 0.0f};
        scene.Registry().emplace<LightComponent>(sun);
    }
    Actor(scene, "Floor", MeshPrimitive::Plane, {0, 0, 0}, {24, 1, 24}, {"/Game/Materials/M_Checker"});
    Actor(scene, "Bumps", MeshPrimitive::Sphere, {-4.5f, 1, 0}, {2, 2, 2}, {"/Game/Materials/M_Bumps"});
    Actor(scene, "GoldRamp", MeshPrimitive::Cube, {-1.5f, 1, 0}, {2, 2, 2}, {"/Game/Materials/M_GoldRamp"});
    Actor(scene, "Leaves", MeshPrimitive::Plane, {1.5f, 1.2f, 0}, {2.2f, 1, 2.2f}, {"/Game/Materials/M_Leaves"});
    Actor(scene, "Emissive", MeshPrimitive::Cylinder, {4.5f, 1, 0}, {1.6f, 2, 1.6f}, {"/Game/Materials/M_Emissive"});
    Actor(scene, "BlueChecker", MeshPrimitive::Cube, {0, 1, -4}, {2, 2, 2}, {"/Game/Materials/MI_Checker_Blue"});
    Actor(scene, "Default", MeshPrimitive::Sphere, {3, 1, -4}, {2, 2, 2}, {});
    Actor(scene, "Unlit", MeshPrimitive::Cube, {-3, 1, -4}, {2, 2, 2}, {"/Game/Materials/M_Unlit"});
    Actor(scene, "CopperORM", MeshPrimitive::Sphere, {-6, 1, -4}, {2, 2, 2}, {"/Game/Materials/M_CopperORM"});
    // The leaf card stands upright facing the camera.
    auto names = scene.Registry().view<NameComponent>();
    for (Entity e : names) {
        if (names.get<NameComponent>(e).name == "Leaves") {
            scene.Registry().get<TransformComponent>(e).rotationEuler = {90.0f, 0.0f, 0.0f};
        }
    }
    REQUIRE(SceneSerializer::SaveToFile(scene, scenes / "Main.lscene"));
}
