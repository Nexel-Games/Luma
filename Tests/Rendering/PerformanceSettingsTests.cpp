// Graphics settings (persistence, sanitizing, tiers), the frame profiler, the
// frame limiter and the mesh LOD cache.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "Luma/Graphics/GraphicsSettings.h"
#include "Luma/Mesh/MeshLodCache.h"
#include "Luma/Profiling/FrameLimiter.h"
#include "Luma/Profiling/Profiler.h"
#include "Luma/Serialization/Json.h"

using namespace Luma;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

namespace {
fs::path TempPath(const char* name) {
    const fs::path p = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}
}  // namespace

TEST_CASE("Graphics settings round-trip through their Config sections", "[graphics][settings]") {
    Graphics::DisplaySettings d;
    d.vsync = true;
    d.presentMode = PresentMode::Immediate;
    d.frameRateLimit = 144.0f;
    d.backgroundFrameRateLimit = 20.0f;
    d.realtimeViewport = false;
    CHECK(Graphics::LoadDisplay(Graphics::SaveDisplay(d)) == d);

    Graphics::RenderingSettings r;
    r.renderScale = 0.75f;
    r.antiAliasing = Graphics::AntiAliasing::None;
    r.shadowMapSize = 1024;
    r.maxShadowCascades = 2;
    r.meshLods = false;
    r.lodBias = 2;
    r.textureMipBias = 0.5f;
    r.tier = Graphics::MatchingTier(r);
    CHECK(Graphics::LoadRendering(Graphics::SaveRendering(r)) == r);

    // Missing sections / keys give the defaults.
    CHECK(Graphics::LoadDisplay(SerialValue{}) == Graphics::DisplaySettings{});
    CHECK(Graphics::LoadRendering(SerialValue::MakeObject()) == Graphics::RenderingSettings{});
}

TEST_CASE("Out-of-range and unknown values are sanitized", "[graphics][settings]") {
    SerialValue display = SerialValue::MakeObject();
    display["presentMode"] = std::string("Warp");
    display["frameRateLimit"] = 99999.0;
    display["backgroundFrameRateLimit"] = 2.0;  // below the usable minimum
    const Graphics::DisplaySettings d = Graphics::LoadDisplay(display);
    CHECK(d.presentMode == Graphics::DisplaySettings{}.presentMode);
    CHECK(d.frameRateLimit == 1000.0f);
    CHECK(d.backgroundFrameRateLimit == 5.0f);

    SerialValue rendering = SerialValue::MakeObject();
    rendering["renderScale"] = 9.0;
    rendering["shadowMapSize"] = static_cast<i64>(3000);
    rendering["maxShadowCascades"] = static_cast<i64>(9);
    rendering["maxAnisotropy"] = static_cast<i64>(5);
    rendering["lodBias"] = static_cast<i64>(-7);
    const Graphics::RenderingSettings r = Graphics::LoadRendering(rendering);
    CHECK(r.renderScale == 2.0f);
    CHECK(r.shadowMapSize == 2048);  // nearest power of two
    CHECK(r.maxShadowCascades == 4);
    CHECK(r.maxAnisotropy == 4);
    CHECK(r.lodBias == -2);
}

TEST_CASE("Quality tiers apply presets and are recognised", "[graphics][settings]") {
    Graphics::RenderingSettings s;
    Graphics::ApplyTier(s, Graphics::QualityTier::Low);
    CHECK(s.renderScale == 0.75f);
    CHECK(s.shadowMapSize == 1024);
    CHECK(Graphics::MatchingTier(s) == Graphics::QualityTier::Low);
    s.shadowMapSize = 4096;  // edited by hand
    CHECK(Graphics::MatchingTier(s) == Graphics::QualityTier::Custom);
    // Culling / LOD settings are not part of a tier.
    Graphics::ApplyTier(s, Graphics::QualityTier::Epic);
    s.meshLods = false;
    CHECK(Graphics::MatchingTier(s) == Graphics::QualityTier::Epic);
    const Renderer2::RenderQuality q = Graphics::ToRenderQuality(s);
    CHECK(q.shadowMapSize == 4096);
    CHECK_FALSE(q.meshLods);
}

TEST_CASE("Present mode and frame-rate caps follow VSync and focus", "[graphics][settings]") {
    Graphics::DisplaySettings d;
    d.presentMode = PresentMode::Mailbox;
    CHECK(Graphics::EffectivePresentMode(d) == PresentMode::Mailbox);
    d.vsync = true;
    CHECK(Graphics::EffectivePresentMode(d) == PresentMode::Fifo);
    d.frameRateLimit = 0.0f;
    d.backgroundFrameRateLimit = 30.0f;
    CHECK(Graphics::EffectiveFrameRateLimit(d, true) == 0.0f);
    CHECK(Graphics::EffectiveFrameRateLimit(d, false) == 30.0f);
    d.frameRateLimit = 20.0f;  // the background cap never raises the rate
    CHECK(Graphics::EffectiveFrameRateLimit(d, false) == 20.0f);
}

TEST_CASE("Graphics.json loads both pages and rejects broken files", "[graphics][settings]") {
    const fs::path dir = TempPath("luma_graphics_cfg");
    Graphics::GraphicsSettings out;
    CHECK(Graphics::LoadFromConfigDir(dir, out));  // missing file = defaults
    CHECK(out == Graphics::GraphicsSettings{});

    Graphics::GraphicsSettings saved;
    saved.display.vsync = true;
    saved.rendering.lodBias = 1;
    SerialValue doc = SerialValue::MakeObject();
    doc["fileVersion"] = 1;
    doc[std::string(Graphics::kDisplayPageId)] = Graphics::SaveDisplay(saved.display);
    doc[std::string(Graphics::kRenderingPageId)] = Graphics::SaveRendering(saved.rendering);
    std::ofstream(dir / Graphics::kConfigFile) << WriteJson(doc, true);
    REQUIRE(Graphics::LoadFromConfigDir(dir, out));
    CHECK(out.display.vsync);
    CHECK(out.rendering.lodBias == 1);

    std::ofstream(dir / Graphics::kConfigFile, std::ios::trunc) << "{ not json";
    std::string error;
    CHECK_FALSE(Graphics::LoadFromConfigDir(dir, out, &error));
    CHECK_FALSE(error.empty());
    CHECK(out == Graphics::GraphicsSettings{});
    fs::remove_all(dir);
}

TEST_CASE("Profiler scopes nest, average over frames and expire silent GPU sources", "[profiling]") {
    Profiling::Profiler p;
    p.SetAverageWindow(1e-9);  // every frame closes a window
    for (int frame = 0; frame < 3; ++frame) {
        p.BeginFrame();
        p.PushScope("Outer");
        p.PushScope("Inner");
        p.PopScope();
        p.PopScope();
        p.Counters().drawCalls += 10;
        if (frame == 0) p.SubmitGpuTimings("Viewport", {{"Viewport", 2.0, 0}, {"G-Buffer", 1.5, 1}});
        p.EndFrame();
    }
    const Profiling::FrameStats& last = p.LastFrame();
    REQUIRE(last.cpu.size() == 2);
    CHECK(last.cpu[0].name == "Outer");
    CHECK(last.cpu[0].depth == 0);
    CHECK(last.cpu[1].depth == 1);
    CHECK(last.counters.drawCalls == 10);
    CHECK(last.gpuMs == 2.0);  // depth-0 passes only
    CHECK(p.Average().counters.drawCalls == 10);

    p.StartCapture();
    for (int frame = 0; frame < 10; ++frame) {
        p.BeginFrame();
        p.EndFrame();
    }
    CHECK(p.TotalAverage().frames == 10);
    CHECK(p.LastFrame().gpu.empty());  // "Viewport" stopped submitting: dropped
}

TEST_CASE("Frame limiter schedules frames without drift or catch-up bursts", "[profiling]") {
    using Clock = Profiling::FrameLimiter::Clock;
    const Clock::time_point t0{};
    const auto budget = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 100.0));
    CHECK(Profiling::FrameLimiter::WaitTime(t0, t0, 0.0) == Clock::duration::zero());  // uncapped
    CHECK(Profiling::FrameLimiter::WaitTime(t0, t0 + budget / 4, 100.0) == budget - budget / 4);
    CHECK(Profiling::FrameLimiter::WaitTime(t0, t0 + budget * 2, 100.0) == Clock::duration::zero());
    // On time: exact cadence. Slightly late: keep the cadence. Very late:
    // restart from now (no burst of short frames).
    CHECK(Profiling::FrameLimiter::NextFrameStart(t0, t0 + budget / 2, 100.0) == t0 + budget);
    CHECK(Profiling::FrameLimiter::NextFrameStart(t0, t0 + budget + budget / 2, 100.0) == t0 + budget);
    CHECK(Profiling::FrameLimiter::NextFrameStart(t0, t0 + budget * 3, 100.0) == t0 + budget * 3);

    Profiling::FrameLimiter limiter;
    limiter.Wait(200.0);  // first call starts the schedule
    const auto start = Clock::now();
    for (int i = 0; i < 5; ++i) limiter.Wait(200.0);
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    CHECK(ms > 20.0);  // 5 frames at 200 FPS = 25 ms (a little sleep jitter allowed)
    CHECK(ms < 60.0);
}

TEST_CASE("Mesh LOD cache round-trips and rejects stale or corrupt files", "[mesh][lod]") {
    const fs::path dir = TempPath("luma_lod_cache");
    const fs::path meshFile = dir / "SM_Rock.lmesh";
    std::ofstream(meshFile) << "mesh bytes";
    MeshData mesh;
    mesh.vertices.resize(4);
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.sections = {{0, 6, "Rock"}};
    const auto key = Mesh::MakeLodCacheKey(meshFile, mesh);
    REQUIRE(key);
    std::vector<MeshLodData> lods(1);
    lods[0].indices = {0, 1, 2};
    lods[0].sections = {{0, 3, "Rock"}};
    lods[0].screenSize = 0.3f;
    const fs::path cache = dir / "rock.lods";
    REQUIRE(Mesh::WriteLodCache(cache, *key, lods));

    const auto read = Mesh::ReadLodCache(cache, *key);
    REQUIRE(read);
    REQUIRE(read->size() == 1);
    CHECK((*read)[0].indices == lods[0].indices);
    CHECK((*read)[0].sections[0].indexCount == 3);
    CHECK_THAT((*read)[0].screenSize, WithinAbs(0.3, 1e-6));

    // Another source (re-imported mesh: size changed) doesn't match.
    Mesh::LodCacheKey other = *key;
    other.sourceSize += 1;
    CHECK_FALSE(Mesh::ReadLodCache(cache, other));
    other = *key;
    other.vertexCount = 3;  // index 3 would be out of range
    CHECK_FALSE(Mesh::ReadLodCache(cache, other));
    // Truncated file.
    const auto size = fs::file_size(cache);
    fs::resize_file(cache, size - 2);
    CHECK_FALSE(Mesh::ReadLodCache(cache, *key));
    CHECK_FALSE(Mesh::ReadLodCache(dir / "missing.lods", *key));
    fs::remove_all(dir);
}
