// SpinBox / SpinBoxInt / VectorField: Unreal-style numeric entry. Click to
// type (math expressions), drag to scrub with Shift/Ctrl multipliers,
// hard limits, snapping, pointer lock while scrubbing.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>

#include "Luma/Core/Types.h"
#include "Luma/Slate/Context.h"
#include "SlateTestHarness.h"

using Catch::Matchers::WithinAbs;
using Luma::f32;
using Luma::i32;
using Luma::Slate::Context;
using Luma::Slate::NumericResult;
using Luma::Slate::Rect;
using Luma::Slate::SpinBoxOptions;
using Luma::Slate::Test::Harness;

namespace {
constexpr int kEnter = 257, kEscape = 256, kTab = 258, kCtrl = 341, kShift = 340;
const Rect kBox{100, 100, 120, 24};
constexpr f32 kCx = 160.0f, kCy = 112.0f;

void Type(Context& ctx, std::string_view s) {
    for (char c : s) ctx.OnText(static_cast<Luma::u32>(c));
}
void Key(Context& ctx, int key) {
    ctx.OnKey(key, true);
    ctx.OnKey(key, false);
}
SpinBoxOptions Speed(double s) {
    SpinBoxOptions o;
    o.speed = s;
    return o;
}
}  // namespace

TEST_CASE("Click without drag enters text mode with all selected", "[slate][spinbox]") {
    Harness h;
    f32 v = 12.5f;
    auto draw = [&] { h.ctx.SpinBox(1, kBox, v); };
    h.Click(kCx, kCy, draw);
    CHECK(h.ctx.IsFieldFocused(1));
    h.Frame(draw);
    Type(h.ctx, "5");
    h.Frame(draw);
    Key(h.ctx, kEnter);
    h.Frame(draw);
    CHECK(v == 5.0f);
    CHECK_FALSE(h.ctx.IsFieldFocused(1));
}

TEST_CASE("Typing an expression and Enter commits its value", "[slate][spinbox]") {
    Harness h;
    f32 v = 0.0f;
    NumericResult res;
    auto draw = [&] { res = h.ctx.SpinBox(1, kBox, v); };
    h.Click(kCx, kCy, draw);
    h.Frame(draw);
    Type(h.ctx, "2*3");
    h.Frame(draw);
    CHECK(v == 0.0f);  // committed only on Enter
    Key(h.ctx, kEnter);
    h.Frame(draw);
    CHECK(v == 6.0f);
    CHECK(res.committed);
    CHECK(res.changed);
}

TEST_CASE("Escape in text mode reverts", "[slate][spinbox]") {
    Harness h;
    f32 v = 1.0f;
    auto draw = [&] { h.ctx.SpinBox(1, kBox, v); };
    h.Click(kCx, kCy, draw);
    h.Frame(draw);
    Type(h.ctx, "9");
    h.Frame(draw);
    Key(h.ctx, kEscape);
    h.Frame(draw);
    CHECK(v == 1.0f);
    CHECK_FALSE(h.ctx.IsFieldFocused(1));
}

TEST_CASE("Invalid text reverts on commit", "[slate][spinbox]") {
    Harness h;
    f32 v = 3.0f;
    NumericResult res;
    auto draw = [&] { res = h.ctx.SpinBox(1, kBox, v); };
    h.Click(kCx, kCy, draw);
    h.Frame(draw);
    Type(h.ctx, "abc");
    h.Frame(draw);
    Key(h.ctx, kEnter);
    h.Frame(draw);
    CHECK(v == 3.0f);
    CHECK_FALSE(res.changed);
}

TEST_CASE("Dragging scrubs by speed per pixel", "[slate][spinbox]") {
    Harness h;
    f32 v = 0.0f;
    h.Drag(kCx, kCy, 10.0f, 0.0f, [&] { h.ctx.SpinBox(1, kBox, v, Speed(0.1)); });
    CHECK_THAT(v, WithinAbs(1.0, 1e-5));
    CHECK_FALSE(h.ctx.IsFieldFocused(1));  // a drag never enters text mode
}

TEST_CASE("Shift multiplies and Ctrl divides the drag", "[slate][spinbox]") {
    Harness h;
    f32 v = 0.0f;
    h.ctx.OnKey(kShift, true);
    h.Drag(kCx, kCy, 10.0f, 0.0f, [&] { h.ctx.SpinBox(1, kBox, v, Speed(0.1)); });
    h.ctx.OnKey(kShift, false);
    CHECK_THAT(v, WithinAbs(10.0, 1e-4));
    v = 0.0f;
    h.ctx.OnKey(kCtrl, true);
    h.Drag(kCx, kCy, 10.0f, 0.0f, [&] { h.ctx.SpinBox(1, kBox, v, Speed(0.1)); });
    h.ctx.OnKey(kCtrl, false);
    CHECK_THAT(v, WithinAbs(0.1, 1e-5));
}

TEST_CASE("min and max clamp drags and typed values", "[slate][spinbox]") {
    Harness h;
    f32 v = 0.0f;
    SpinBoxOptions o = Speed(1.0);
    o.min = -1.0;
    o.max = 5.0;
    auto draw = [&] { h.ctx.SpinBox(1, kBox, v, o); };
    h.Drag(kCx, kCy, 10.0f, 0.0f, draw);
    CHECK(v == 5.0f);
    h.Click(kCx, kCy, draw);
    h.Frame(draw);
    Type(h.ctx, "-100");
    h.Frame(draw);
    Key(h.ctx, kEnter);
    h.Frame(draw);
    CHECK(v == -1.0f);
}

TEST_CASE("delta snaps the value", "[slate][spinbox]") {
    Harness h;
    f32 v = 0.0f;
    SpinBoxOptions o = Speed(0.01);
    o.delta = 0.25;
    h.Drag(kCx, kCy, 20.0f, 0.0f, [&] { h.ctx.SpinBox(1, kBox, v, o); });
    CHECK(v == 0.25f);
}

TEST_CASE("dragStarted and dragEnded fire once each", "[slate][spinbox]") {
    Harness h;
    f32 v = 0.0f;
    int started = 0, ended = 0;
    auto draw = [&] {
        const NumericResult r = h.ctx.SpinBox(1, kBox, v);
        started += r.dragStarted;
        ended += r.dragEnded;
    };
    h.ctx.OnMouseMove(kCx, kCy);
    h.ctx.OnMouseButton(0, true);
    h.Frame(draw);
    for (int i = 1; i <= 4; ++i) {
        h.ctx.OnMouseMove(kCx + 5.0f * static_cast<f32>(i), kCy);
        h.Frame(draw);
    }
    h.ctx.OnMouseButton(0, false);
    h.Frame(draw);
    h.Frame(draw);
    CHECK(started == 1);
    CHECK(ended == 1);
}

TEST_CASE("Pointer lock while dragging, restore point on release", "[slate][spinbox]") {
    Harness h;
    f32 v = 0.0f;
    auto draw = [&] { h.ctx.SpinBox(1, kBox, v); };
    h.ctx.OnMouseMove(kCx, kCy);
    h.ctx.OnMouseButton(0, true);
    h.Frame(draw);
    CHECK_FALSE(h.ctx.PointerLockRequested());
    h.ctx.OnMouseMove(kCx + 20.0f, kCy);
    h.Frame(draw);
    CHECK(h.ctx.PointerLockRequested());
    h.ctx.OnMouseButton(0, false);
    h.Frame(draw);
    CHECK_FALSE(h.ctx.PointerLockRequested());
    const auto restore = h.ctx.TakePointerRestore();
    REQUIRE(restore.has_value());
    CHECK(restore->x == kCx);
    CHECK(restore->y == kCy);
}

TEST_CASE("Drag ends and lock releases when the widget is not drawn", "[slate][spinbox]") {
    Harness h;
    f32 v = 0.0f;
    auto draw = [&] { h.ctx.SpinBox(1, kBox, v); };
    h.ctx.OnMouseMove(kCx, kCy);
    h.ctx.OnMouseButton(0, true);
    h.Frame(draw);
    h.ctx.OnMouseMove(kCx + 20.0f, kCy);
    h.Frame(draw);
    REQUIRE(h.ctx.PointerLockRequested());
    h.Frame([] {});  // panel closed mid-drag
    h.Frame([] {});
    CHECK_FALSE(h.ctx.PointerLockRequested());
    CHECK(h.ctx.TakePointerRestore().has_value());
    // Another widget can take the mouse once the button is released.
    h.ctx.OnMouseButton(0, false);
    h.Frame([] {});
    bool toggled = false;
    h.Click(400, 400, [&] { h.ctx.Toggle(9, {390, 390, 40, 20}, toggled); });
    CHECK(toggled);
}

TEST_CASE("An external value change does not overwrite typing", "[slate][spinbox]") {
    Harness h;
    f32 v = 1.0f;
    auto draw = [&] { h.ctx.SpinBox(1, kBox, v); };
    h.Click(kCx, kCy, draw);
    h.Frame(draw);
    Type(h.ctx, "7");
    h.Frame(draw);
    v = 99.0f;  // e.g. another panel edited the value
    h.Frame(draw);
    Key(h.ctx, kEnter);
    h.Frame(draw);
    CHECK(v == 7.0f);
}

TEST_CASE("SpinBoxInt rounds drags and typed values", "[slate][spinbox]") {
    Harness h;
    i32 v = 0;
    auto draw = [&] { h.ctx.SpinBoxInt(1, kBox, v, Speed(0.1)); };
    h.Drag(kCx, kCy, 15.0f, 0.0f, draw);
    CHECK(v == 2);
    h.Click(kCx, kCy, draw);
    h.Frame(draw);
    Type(h.ctx, "2.6");
    h.Frame(draw);
    Key(h.ctx, kEnter);
    h.Frame(draw);
    CHECK(v == 3);
}

TEST_CASE("Tab into a spin box selects all its text", "[slate][spinbox]") {
    Harness h;
    f32 a = 1.0f, b = 2.0f;
    auto draw = [&] {
        h.ctx.SpinBox(1, kBox, a);
        h.ctx.SpinBox(2, {100, 140, 120, 24}, b);
    };
    h.Click(kCx, kCy, draw);
    h.Frame(draw);
    Key(h.ctx, kTab);
    h.Frame(draw);
    CHECK(h.ctx.IsFieldFocused(2));
    h.Frame(draw);
    Type(h.ctx, "4");
    h.Frame(draw);
    Key(h.ctx, kEnter);
    h.Frame(draw);
    CHECK(b == 4.0f);
    CHECK(a == 1.0f);
}

TEST_CASE("DragFloat still scrubs", "[slate][spinbox]") {
    Harness h;
    f32 v = 0.0f;
    h.Drag(kCx, kCy, 10.0f, 0.0f, [&] { h.ctx.DragFloat(1, kBox, v, 0.1f); });
    CHECK_THAT(v, WithinAbs(1.0, 1e-5));
}

TEST_CASE("Escape cancels a scrub and restores the start value", "[slate][spinbox]") {
    Harness h;
    f32 v = 2.0f;
    int ended = 0;
    auto draw = [&] { ended += h.ctx.SpinBox(1, kBox, v, Speed(0.1)).dragEnded; };
    h.ctx.OnMouseMove(kCx, kCy);
    h.ctx.OnMouseButton(0, true);
    h.Frame(draw);
    h.ctx.OnMouseMove(kCx + 30.0f, kCy);
    h.Frame(draw);
    REQUIRE(v != 2.0f);
    Key(h.ctx, kEscape);
    h.Frame(draw);
    CHECK(v == 2.0f);
    CHECK(ended == 1);
    CHECK(h.ctx.TakePointerRestore().has_value());
    h.ctx.OnMouseButton(0, false);
    h.Frame(draw);
    CHECK_FALSE(h.ctx.IsFieldFocused(1));  // releasing after a cancel doesn't start typing
}

TEST_CASE("VectorField 4 edits the fourth cell", "[slate][spinbox]") {
    Harness h;
    f32 v[4] = {0, 0, 0, 0};
    h.Drag(450.0f, 112.0f, 10.0f, 0.0f,
           [&] { h.ctx.VectorField(7, {100, 100, 400, 24}, v, 4, Speed(0.1)); });
    CHECK(v[0] == 0.0f);
    CHECK(v[1] == 0.0f);
    CHECK(v[2] == 0.0f);
    CHECK_THAT(v[3], WithinAbs(1.0, 1e-5));
}

TEST_CASE("Vector3Field still scrubs its cells", "[slate][spinbox]") {
    Harness h;
    f32 v[3] = {0, 0, 0};
    // Middle cell of a 300 px field.
    h.Drag(250.0f, 112.0f, 10.0f, 0.0f, [&] { h.ctx.Vector3Field(7, {100, 100, 300, 24}, v); });
    CHECK(v[0] == 0.0f);
    CHECK(v[1] > 0.0f);
    CHECK(v[2] == 0.0f);
}

TEST_CASE("Tab walks a vector field's cells X to Y to Z", "[slate][spinbox]") {
    Harness h;
    f32 v[3] = {1, 2, 3};
    auto draw = [&] { h.ctx.VectorField(7, {100, 100, 300, 24}, v, 3); };
    h.Click(120.0f, 112.0f, draw);  // X cell
    h.Frame(draw);
    Key(h.ctx, kTab);
    h.Frame(draw);
    h.Frame(draw);
    Type(h.ctx, "5");
    h.Frame(draw);
    Key(h.ctx, kTab);
    h.Frame(draw);
    h.Frame(draw);
    Type(h.ctx, "6");
    h.Frame(draw);
    Key(h.ctx, kEnter);
    h.Frame(draw);
    CHECK(v[0] == 1.0f);
    CHECK(v[1] == 5.0f);  // committed when Tab moved focus on
    CHECK(v[2] == 6.0f);
}

TEST_CASE("A click whose press and release land in one frame enters text mode",
          "[slate][spinbox]") {
    Harness h;
    f32 v = 3.0f;
    auto draw = [&] { h.ctx.SpinBox(1, kBox, v); };
    h.ctx.OnMouseMove(kCx, kCy);
    h.ctx.OnMouseButton(0, true);
    h.ctx.OnMouseButton(0, false);
    h.Frame(draw);
    CHECK(h.ctx.IsFieldFocused(1));
    h.Frame(draw);
    Type(h.ctx, "2*4");
    h.Frame(draw);
    Key(h.ctx, kEnter);
    h.Frame(draw);
    CHECK(v == 8.0f);
}
