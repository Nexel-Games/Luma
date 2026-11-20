// While a modal dialog is open, widgets drawn before it (the panels) see no
// input; only the modal's own widgets do.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Luma/Core/Types.h"
#include "Luma/Slate/Context.h"

using Luma::Slate::Context;
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

}  // namespace

TEST_CASE("An open modal blocks input to the widgets under it", "[slate][modal]") {
    NullRenderer renderer;
    Context ui;
    ui.Init(renderer, TestTypography());
    int panelClicks = 0, modalClicks = 0;
    Rect modalButton{};
    const Rect panelButton{600, 340, 80, 40};  // under the centered modal
    auto frame = [&](bool modal) {
        ui.BeginFrame(1280, 720, 0.0166f);
        if (ui.Button(Context::ID("panel"), panelButton, "Panel")) ++panelClicks;
        if (modal) {
            const Rect content = ui.BeginModal(Context::ID("dlg"), "Dialog", {400, 200});
            modalButton = {content.x, content.y, 100, 30};
            if (ui.Button(Context::ID("ok"), modalButton, "OK")) ++modalClicks;
        }
        ui.EndFrame();
    };
    auto click = [&](Luma::f32 x, Luma::f32 y, bool modal) {
        ui.OnMouseMove(x, y);
        frame(modal);
        ui.OnMouseButton(0, true);
        frame(modal);
        ui.OnMouseButton(0, false);
        frame(modal);
    };

    frame(true);  // the modal opens
    click(640, 360, true);
    CHECK(panelClicks == 0);
    click(modalButton.x + 10, modalButton.y + 10, true);
    CHECK(modalClicks == 1);
    ui.EndModal();
    click(640, 360, false);
    CHECK(panelClicks == 1);
}
