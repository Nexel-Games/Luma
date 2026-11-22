// Slate drag and drop: a source offers a payload while the button is held,
// the drag starts after a small movement threshold, and a target accepts the
// payload once on release (only for its type); Escape cancels.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Luma/Core/Types.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/DragDrop.h"

using Luma::f32;
using Luma::Slate::Context;
using Luma::Slate::DragPayload;
using Luma::Slate::Rect;

namespace {

class NullRenderer final : public Luma::Renderer {
public:
    void OnResize(Luma::u32, Luma::u32) override {}
    void SetClearColor(const Luma::ClearColor&) override {}
    bool BeginFrame() override { return true; }
    void EndFrame() override {}
    void DrawUI(const Luma::UIDrawData&) override {}
    Luma::TextureHandle CreateTexture(Luma::u32, Luma::u32, const void*) override { return 1; }
    void DestroyTexture(Luma::TextureHandle) override {}
    void CaptureFrame(const std::string&) override {}
    void WaitIdle() override {}
};

Luma::Slate::Typography TestTypography() {
    Luma::Slate::Typography t;
    t.uiRegular = "C:/Windows/Fonts/segoeui.ttf";
    t.uiMedium = t.uiSemiBold = t.uiBold = t.mono = t.uiRegular;
    return t;
}

const Rect kSource{100, 100, 80, 80};
const Rect kTarget{400, 100, 120, 60};
constexpr int kKeyEscape = 256;

// One frame with a drag source (an asset tile) and a drop target (a slot).
struct DragFixture {
    NullRenderer renderer;
    Context ui;
    int accepted = 0;
    std::string acceptedData;
    std::string targetType = "material";

    DragFixture() { ui.Init(renderer, TestTypography()); }

    void Frame() {
        ui.BeginFrame(1280, 720, 0.0166f);
        DragPayload payload;
        payload.type = "material";
        payload.data = "/Game/Materials/M_Wood";
        payload.label = "M_Wood";
        ui.BeginDrag(Context::ID("tile"), kSource, payload);
        if (const DragPayload* dropped = ui.AcceptDrop(kTarget, targetType)) {
            ++accepted;
            acceptedData = dropped->data;
        }
        ui.EndFrame();
    }
    void Move(f32 x, f32 y) {
        ui.OnMouseMove(x, y);
        Frame();
    }
    void Press(f32 x, f32 y) {
        ui.OnMouseMove(x, y);
        ui.OnMouseButton(0, true);
        Frame();
    }
    void Release() {
        ui.OnMouseButton(0, false);
        Frame();
    }
};

}  // namespace

TEST_CASE("Drag starts only past the movement threshold", "[slate][dragdrop]") {
    DragFixture f;
    f.Press(140, 140);
    f.Move(143, 140);
    CHECK_FALSE(f.ui.IsDragging("material"));
    f.Move(150, 140);
    CHECK(f.ui.IsDragging("material"));
    CHECK_FALSE(f.ui.IsDragging("mesh"));
}

TEST_CASE("Releasing over a matching target delivers the payload once", "[slate][dragdrop]") {
    DragFixture f;
    f.Press(140, 140);
    f.Move(200, 140);
    f.Move(450, 130);
    CHECK(f.accepted == 0);  // hovering does not drop
    f.Release();
    CHECK(f.accepted == 1);
    CHECK(f.acceptedData == "/Game/Materials/M_Wood");
    CHECK_FALSE(f.ui.IsDragging("material"));
    f.Frame();
    CHECK(f.accepted == 1);
}

TEST_CASE("Targets ignore payloads of another type", "[slate][dragdrop]") {
    DragFixture f;
    f.targetType = "mesh";
    f.Press(140, 140);
    f.Move(450, 130);
    f.Release();
    CHECK(f.accepted == 0);
}

TEST_CASE("Releasing outside every target cancels the drag", "[slate][dragdrop]") {
    DragFixture f;
    f.Press(140, 140);
    f.Move(700, 600);
    f.Release();
    CHECK(f.accepted == 0);
    CHECK_FALSE(f.ui.IsDragging("material"));
}

TEST_CASE("Escape cancels a drag", "[slate][dragdrop]") {
    DragFixture f;
    f.Press(140, 140);
    f.Move(450, 130);
    REQUIRE(f.ui.IsDragging("material"));
    f.ui.OnKey(kKeyEscape, true);
    f.Frame();
    CHECK_FALSE(f.ui.IsDragging("material"));
    f.Release();
    CHECK(f.accepted == 0);
}

TEST_CASE("A press outside the source starts no drag", "[slate][dragdrop]") {
    DragFixture f;
    f.Press(300, 300);
    f.Move(450, 130);
    CHECK_FALSE(f.ui.IsDragging("material"));
}

TEST_CASE("The active drag's payload is readable while dragging", "[slate][dragdrop]") {
    DragFixture f;
    CHECK(f.ui.ActiveDrag() == nullptr);
    f.Press(140, 140);
    CHECK(f.ui.ActiveDrag() == nullptr);  // not past the threshold yet
    f.Move(200, 140);
    const DragPayload* active = f.ui.ActiveDrag();
    REQUIRE(active != nullptr);
    CHECK(active->type == "material");
    CHECK(active->data == "/Game/Materials/M_Wood");
    f.ui.OnKey(kKeyEscape, true);
    f.Frame();
    f.ui.OnKey(kKeyEscape, false);
    f.Frame();
    CHECK(f.ui.ActiveDrag() == nullptr);  // Escape cancelled it
}
