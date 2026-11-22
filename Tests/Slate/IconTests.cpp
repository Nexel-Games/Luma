#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "Luma/Slate/Icons.h"
#include "SlateTestHarness.h"

using namespace Luma;
using namespace Luma::Slate;

TEST_CASE("Animate converges to target over frames", "[slate][anim]") {
    Test::Harness harness;
    auto& ctx = harness.ctx;
    // First call: not hovered yet -> value stays at 0.
    f32 v0 = ctx.Animate(42, false, 14.0f);
    REQUIRE(v0 == 0.0f);
    // Manually advance the frame counter by calling BeginFrame.
    ctx.BeginFrame(800, 600, 0.0166f);
    f32 v1 = ctx.Animate(42, true, 14.0f);
    ctx.EndFrame();
    REQUIRE(v1 > 0.0f);
    REQUIRE(v1 < 1.0f);
    // After many frames, value converges to ~1.
    for (int i = 0; i < 200; ++i) {
        ctx.BeginFrame(800, 600, 0.0166f);
        ctx.Animate(42, true, 14.0f);
        ctx.EndFrame();
    }
    ctx.BeginFrame(800, 600, 0.0166f);
    f32 vFinal = ctx.Animate(42, true, 14.0f);
    ctx.EndFrame();
    REQUIRE(std::fabs(vFinal - 1.0f) < 1e-3f);
}

TEST_CASE("Every editor icon samples its own tile in the packaged texture", "[slate][icon]") {
    Test::Harness harness;
    auto& ctx = harness.ctx;
    REQUIRE(LoadIconAtlas(ctx, harness.renderer, std::string(LUMA_SOURCE_DIR) + "/Content/Editor/Icons/SlateIcons.png"));
    const int count = static_cast<int>(Icon::Count) - 1;
    for (int i = 0; i < count; ++i) {
        CAPTURE(i);
        ctx.BeginFrame(800, 600, 0.016f);
        DrawIcon(ctx, {0, 0, 16, 16}, static_cast<Icon>(i + 1));
        const auto& data = ctx.EndFrame();
        REQUIRE(data.commandCount == 1);
        REQUIRE(data.vertexCount == 4);
        CHECK(data.commands[0].texture == ctx.IconAtlas());
        CHECK(std::fabs(data.vertices[0].u - static_cast<f32>(i) / count) < 1e-6f);
    }
    ctx.BeginFrame(800, 600, 0.016f);
    DrawIcon(ctx, {0, 0, 16, 16}, Icon::None);
    CHECK(ctx.EndFrame().vertexCount == 0);
}
