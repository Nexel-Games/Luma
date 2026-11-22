#pragma once

// Shared harness for Slate widget tests: a Context initialised against a
// renderer stub with a real system font (so text measuring works), plus
// frame / click helpers. Input events are fed between frames, the way the
// app's event pump does.

#include <string>
#include <utility>

#include "Luma/Core/Types.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Theme.h"

namespace Luma::Slate::Test {

class NullRenderer final : public Luma::Renderer {
public:
    void OnResize(u32, u32) override {}
    void SetClearColor(const ClearColor&) override {}
    bool BeginFrame() override { return true; }
    void EndFrame() override {}
    void DrawUI(const UIDrawData&) override {}
    // Non-zero handles so the font atlas exists and text produces geometry.
    TextureHandle CreateTexture(u32, u32, const void*) override { return ++m_nextTexture; }
    void DestroyTexture(TextureHandle) override {}
    void CaptureFrame(const std::string&) override {}
    void WaitIdle() override {}

private:
    TextureHandle m_nextTexture = 0;
};

inline Typography SystemTypography() {
    Typography t;
    t.uiRegular = "C:/Windows/Fonts/segoeui.ttf";
    t.uiMedium = t.uiRegular;
    t.uiSemiBold = t.uiRegular;
    t.uiBold = t.uiRegular;
    t.mono = t.uiRegular;
    return t;
}

struct Harness {
    NullRenderer renderer;
    Context ctx;
    Harness() { ctx.Init(renderer, SystemTypography()); }

    template <typename Fn>
    void Frame(Fn&& widgets) {
        ctx.BeginFrame(800, 600, 0.0166f);
        widgets();
        ctx.EndFrame();
    }
    // Press in one frame, release in the next (two frames).
    template <typename Fn>
    void Click(f32 x, f32 y, Fn&& widgets) {
        ctx.OnMouseMove(x, y);
        ctx.OnMouseButton(0, true);
        Frame(widgets);
        ctx.OnMouseButton(0, false);
        Frame(widgets);
    }
    // Press, move by (dx, dy) over one frame, release.
    template <typename Fn>
    void Drag(f32 x, f32 y, f32 dx, f32 dy, Fn&& widgets) {
        ctx.OnMouseMove(x, y);
        ctx.OnMouseButton(0, true);
        Frame(widgets);
        ctx.OnMouseMove(x + dx, y + dy);
        Frame(widgets);
        ctx.OnMouseButton(0, false);
        Frame(widgets);
    }
};

}  // namespace Luma::Slate::Test
