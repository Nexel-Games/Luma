// Anti-aliased DrawList geometry (opt-in; the default stays exact, see
// Tests/Slate/DrawListTests.cpp).
#include <catch2/catch_test_macros.hpp>

#include "Luma/Slate/DrawList.h"

using Luma::Slate::Color;
using Luma::Slate::DrawList;

namespace {
bool HasTransparentVertex(const Luma::UIDrawData& d) {
    for (Luma::u32 i = 0; i < d.vertexCount; ++i)
        if ((d.vertices[i].color >> 24) == 0) return true;
    return false;
}
}  // namespace

TEST_CASE("Anti-aliasing is off by default and adds a fading fringe when on", "[slate][draw][aa]") {
    DrawList dl;
    dl.Begin(800, 600);
    CHECK_FALSE(dl.AntiAliased());
    dl.AddCircleFilled({50, 50}, 20.0f, Color::RGB(255, 255, 255), 16);
    CHECK(dl.vertexCount() == 16);
    CHECK_FALSE(HasTransparentVertex(dl.Build()));

    DrawList aa;
    aa.SetAntiAliased(true);
    aa.Begin(800, 600);
    aa.AddCircleFilled({50, 50}, 20.0f, Color::RGB(255, 255, 255), 16);
    const auto& d = aa.Build();
    CHECK(d.vertexCount >= 2 * 30);  // radius-adaptive segments, core + fringe ring
    CHECK(HasTransparentVertex(d));
    CHECK(d.indexCount % 3 == 0);
}

TEST_CASE("Anti-aliased rounded outline is one continuous stroke", "[slate][draw][aa]") {
    DrawList dl;
    dl.SetAntiAliased(true);
    dl.Begin(800, 600);
    dl.AddRectOutlineRounded({10, 10, 120, 40}, Color::RGB(255, 128, 0), 1.5f, 6.0f);
    const auto& d = dl.Build();
    CHECK(d.commandCount == 1);
    CHECK(d.vertexCount % 4 == 0);                   // 4 vertices per path point
    CHECK(d.indexCount == d.vertexCount / 4 * 18);   // 3 bands x 2 tris per segment, closed
    CHECK(HasTransparentVertex(d));
}

TEST_CASE("Anti-aliased diagonal line and rounded fill", "[slate][draw][aa]") {
    DrawList dl;
    dl.SetAntiAliased(true);
    dl.Begin(800, 600);
    dl.AddLine({10, 10}, {50, 50}, Color::RGB(255, 255, 255), 2.0f);
    CHECK(dl.vertexCount() == 8);
    dl.AddLine({0, 10}, {100, 10}, Color::RGB(255, 255, 255), 2.0f);  // axis-aligned stays a crisp rect
    CHECK(dl.vertexCount() == 12);
    dl.AddRectFilledRounded({0, 0, 100, 40}, Color::RGB(40, 40, 40), 8.0f);
    CHECK(dl.vertexCount() > 12 + 2 * 16);
}
