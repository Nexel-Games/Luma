#include "Luma/Editor/Panels/GraphicsPages.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Luma::Editor::Panels {

namespace {

using Graphics::AntiAliasing;
using Graphics::QualityTier;
using Slate::Rect;

// Lays out label / control rows down a page body. Every row has a tooltip on
// its label (hover the name to read what the setting does).
class RowLayout {
public:
    RowLayout(Slate::Context& ui, const Rect& area, std::string_view idPrefix)
        : m_ui(ui), m_area(area), m_y(area.y), m_prefix(idPrefix) {
        m_labelW = std::min(220.0f, area.w * 0.45f);
    }

    u64 Id(std::string_view key) const { return Slate::Context::ID(std::string(m_prefix) + "." + std::string(key)); }

    void Heading(std::string_view text) {
        if (m_y > m_area.y) m_y += 6.0f;
        m_ui.Panel({m_area.x, m_y, m_area.w, 24}, m_ui.theme().surface3);
        m_ui.Heading({m_area.x + 8, m_y, m_area.w - 8, 24}, text, m_ui.theme().text);
        m_y += 24.0f;
        m_ui.Panel({m_area.x, m_y - 2.0f, m_area.w, 1.0f}, m_ui.theme().separator);
        m_y += 4.0f;
    }

    // Label + tooltip; returns the control rect (right column).
    Rect Row(std::string_view key, std::string_view label, std::string_view tooltip, f32 controlW = 0.0f) {
        const Rect labelRect{m_area.x + 8.0f, m_y, m_labelW - 8.0f, 22.0f};
        m_ui.LabelIn(labelRect, label, m_ui.theme().textDim);
        m_ui.Tooltip(Id(std::string(key) + ".tip"), labelRect, std::string(label) + "\n" + std::string(tooltip));
        const f32 w = controlW > 0.0f ? std::min(controlW, m_area.w - m_labelW) : m_area.w - m_labelW;
        const Rect control{m_area.x + m_labelW, m_y, std::max(w, 60.0f), 22.0f};
        m_y += 28.0f;
        return control;
    }

    void Note(std::string_view text) {
        m_ui.LabelIn({m_area.x + 8.0f, m_y, m_area.w - 8.0f, 20.0f}, text, m_ui.theme().textDim);
        m_y += 22.0f;
    }

    bool Check(std::string_view key, std::string_view label, std::string_view tooltip, bool& value) {
        const Rect r = Row(key, label, tooltip, 22.0f);
        return m_ui.Checkbox(Id(key), {r.x, r.y + 2.0f, 18.0f, 18.0f}, "", value);
    }

    // Combo over `items`; returns true when the index changed.
    bool Choice(std::string_view key, std::string_view label, std::string_view tooltip,
                const std::vector<std::string>& items, int& index) {
        const Rect r = Row(key, label, tooltip, 220.0f);
        const int picked = m_ui.Combo(Id(key), r, items, index);
        if (picked == index || picked < 0 || picked >= static_cast<int>(items.size())) return false;
        index = picked;
        return true;
    }

    bool Number(std::string_view key, std::string_view label, std::string_view tooltip, f32& value,
                const Slate::SpinBoxOptions& options) {
        const Rect r = Row(key, label, tooltip, 160.0f);
        return m_ui.SpinBox(Id(key), r, value, options).changed;
    }

    bool Integer(std::string_view key, std::string_view label, std::string_view tooltip, i32& value,
                 const Slate::SpinBoxOptions& options) {
        const Rect r = Row(key, label, tooltip, 160.0f);
        return m_ui.SpinBoxInt(Id(key), r, value, options).changed;
    }

private:
    Slate::Context& m_ui;
    Rect m_area;
    f32 m_y;
    f32 m_labelW;
    std::string_view m_prefix;
};

int IndexOf(const std::vector<u32>& values, u32 v) {
    const auto it = std::find(values.begin(), values.end(), v);
    return it == values.end() ? 0 : static_cast<int>(it - values.begin());
}

}  // namespace

bool DrawDisplayPage(Slate::Context& ui, const Rect& area, Graphics::DisplaySettings& s,
                     const std::string& activePresentMode) {
    RowLayout rows(ui, area, "ps.display");
    bool changed = false;

    rows.Heading("Presentation");
    changed |= rows.Check("vsync", "VSync",
                          "Present in step with the monitor refresh (FIFO). No tearing, but the frame rate is "
                          "capped at the refresh rate (usually 60 FPS). Overrides Present Mode.",
                          s.vsync);
    {
        const std::vector<std::string> items = {"FIFO (VSync)", "Mailbox (uncapped, no tearing)",
                                                "Immediate (uncapped, may tear)"};
        int index = static_cast<int>(s.presentMode);
        if (rows.Choice("present", "Present Mode",
                        "How finished frames reach the screen when VSync is off. Mailbox: newest frame "
                        "replaces the queued one, no tearing. Immediate: shown at once, lowest latency, may "
                        "tear. Unsupported modes fall back (Mailbox -> Immediate -> FIFO).",
                        items, index)) {
            s.presentMode = static_cast<PresentMode>(index);
            changed = true;
        }
    }
    if (!activePresentMode.empty()) rows.Note("Swapchain is using: " + activePresentMode);

    rows.Heading("Frame Rate");
    Slate::SpinBoxOptions fps;
    fps.min = 0.0;
    fps.max = 1000.0;
    fps.speed = 1.0;
    fps.delta = 1.0;
    fps.maxFractionalDigits = 0;
    fps.minFractionalDigits = 0;
    fps.suffix = " FPS";
    changed |= rows.Number("limit", "Frame Rate Limit",
                           "Maximum frames per second (0 = uncapped). Paced on the CPU with a precise timer, "
                           "independent of VSync.",
                           s.frameRateLimit, fps);
    changed |= rows.Number("bglimit", "Background Frame Rate Limit",
                           "Frame-rate cap while the window is unfocused or minimized (0 = use the limit "
                           "above). Saves power when you work in another application.",
                           s.backgroundFrameRateLimit, fps);
    rows.Note("0 = uncapped. Changes apply immediately.");

    rows.Heading("Editor");
    changed |= rows.Check("realtime", "Realtime Viewport",
                          "Re-render the 3D viewport every frame. When off, the viewport re-renders only when "
                          "the camera, the scene, materials or the viewport size change (plus a short settle "
                          "time for auto exposure), so an idle editor costs almost no GPU time.",
                          s.realtimeViewport);
    if (changed) Graphics::Sanitize(s);
    return changed;
}

bool DrawRenderingPage(Slate::Context& ui, const Rect& area, Graphics::RenderingSettings& s) {
    RowLayout rows(ui, area, "ps.rendering");
    bool changed = false;
    bool tierFieldChanged = false;

    rows.Heading("Quality");
    {
        const std::vector<std::string> items = {"Low", "Medium", "High", "Epic", "Custom"};
        int index = static_cast<int>(s.tier);
        if (rows.Choice("tier", "Quality Tier",
                        "Preset for resolution, anti-aliasing, bloom, shadows and texture filtering. Editing "
                        "any of those settings switches the tier to Custom.",
                        items, index)) {
            Graphics::ApplyTier(s, static_cast<QualityTier>(index));
            changed = true;
        }
    }
    {
        Slate::SpinBoxOptions o;
        o.min = 0.25;
        o.max = 2.0;
        o.sliderMin = 0.25;
        o.sliderMax = 2.0;
        o.delta = 0.05;
        o.maxFractionalDigits = 2;
        o.minFractionalDigits = 2;
        o.suffix = " x";
        tierFieldChanged |= rows.Number(
            "scale", "Render Scale",
            "3D resolution relative to the viewport's pixel size (already DPI-aware). Below 1 renders "
            "fewer pixels (faster, softer); above 1 supersamples (sharper, much slower: 1.5 = 2.25x the pixels).",
            s.renderScale, o);
    }
    {
        const std::vector<std::string> items = {"None", "FXAA"};
        int index = static_cast<int>(s.antiAliasing);
        if (rows.Choice("aa", "Anti-Aliasing",
                        "Edge smoothing. FXAA is a cheap full-screen post filter; None skips the pass.", items,
                        index)) {
            s.antiAliasing = static_cast<AntiAliasing>(index);
            tierFieldChanged = true;
        }
    }
    tierFieldChanged |= rows.Check("bloom", "Bloom",
                                   "Allow bloom (glow around bright areas). Off skips the bloom chain even when a "
                                   "post-process volume enables it.",
                                   s.bloom);
    tierFieldChanged |= rows.Check("autoexp", "Auto Exposure",
                                   "Allow eye adaptation (histogram auto exposure). Off uses the fixed exposure "
                                   "of the post-process volume.",
                                   s.autoExposure);

    rows.Heading("Shadows");
    tierFieldChanged |= rows.Check("shadows", "Sun Shadows",
                                   "Cascaded shadow maps for the sun (first directional light). Off removes every "
                                   "shadow draw.",
                                   s.shadows);
    {
        const std::vector<u32> sizes = {512, 1024, 2048, 4096};
        const std::vector<std::string> items = {"512", "1024", "2048", "4096"};
        int index = IndexOf(sizes, s.shadowMapSize);
        if (rows.Choice("shadowsize", "Shadow Map Size",
                        "Resolution of each shadow cascade (texels per side). Higher is sharper but costs GPU "
                        "memory and fill rate (4096 = 256 MiB for four cascades).",
                        items, index)) {
            s.shadowMapSize = sizes[static_cast<usize>(index)];
            tierFieldChanged = true;
        }
    }
    {
        Slate::SpinBoxOptions o;
        o.min = 1;
        o.max = 4;
        o.speed = 0.05;
        i32 cascades = static_cast<i32>(s.maxShadowCascades);
        if (rows.Integer("cascades", "Max Shadow Cascades",
                         "Upper limit on each light's cascade count. Every cascade redraws the shadow casters it "
                         "covers.",
                         cascades, o)) {
            s.maxShadowCascades = static_cast<u32>(std::clamp(cascades, 1, 4));
            tierFieldChanged = true;
        }
    }
    {
        Slate::SpinBoxOptions o;
        o.min = 5.0;
        o.max = 2000.0;
        o.speed = 1.0;
        o.maxFractionalDigits = 0;
        o.minFractionalDigits = 0;
        o.suffix = " m";
        tierFieldChanged |= rows.Number("shadowdist", "Max Shadow Distance",
                                        "Upper limit on each light's shadow distance. Shorter distances "
                                        "concentrate texels near the camera (sharper) and cull more casters.",
                                        s.maxShadowDistance, o);
    }

    rows.Heading("Culling");
    changed |= rows.Check("frustum", "Frustum Culling",
                          "Skip objects outside the camera's view. Only turn off to debug bounds.",
                          s.frustumCulling);
    changed |= rows.Check("castercull", "Shadow Caster Culling",
                          "Skip shadow casters outside each cascade's volume (casters between the sun and the "
                          "cascade are kept).",
                          s.shadowCasterCulling);

    rows.Heading("Textures (applied on restart)");
    {
        const std::vector<u32> values = {1, 2, 4, 8, 16};
        const std::vector<std::string> items = {"1x (off)", "2x", "4x", "8x", "16x"};
        int index = IndexOf(values, s.maxAnisotropy);
        if (rows.Choice("aniso", "Max Anisotropy",
                        "Anisotropic filtering for material textures that request it (sharper at grazing "
                        "angles). Takes effect the next time the editor starts.",
                        items, index)) {
            s.maxAnisotropy = values[static_cast<usize>(index)];
            tierFieldChanged = true;
        }
    }
    {
        Slate::SpinBoxOptions o;
        o.min = -2.0;
        o.max = 4.0;
        o.speed = 0.05;
        o.delta = 0.25;
        o.maxFractionalDigits = 2;
        changed |= rows.Number("mipbias", "Texture Mip Bias",
                               "Added to the texture LOD (the engine's LOD bias). Positive values sample smaller "
                               "mips: blurrier but less bandwidth. Takes effect the next time the editor starts.",
                               s.textureMipBias, o);
    }

    if (tierFieldChanged) {
        changed = true;
        Graphics::Sanitize(s);
        s.tier = Graphics::MatchingTier(s);
    } else if (changed) {
        Graphics::Sanitize(s);
    }
    return changed;
}

RegistrationToken RegisterDisplayPage(SettingsPageRegistry& pages, Graphics::DisplaySettings& settings,
                                      std::function<std::string()> activePresentMode) {
    SettingsPageDesc page;
    page.id = std::string(Graphics::kDisplayPageId);
    page.category = "Engine";
    page.contentHeight = 286;
    page.title = "Display & Frame Rate";
    page.tooltip = "VSync, present mode and frame-rate limits (Config/Graphics.json).";
    page.owner = "Engine";
    page.configFile = std::string(Graphics::kConfigFile);
    Graphics::DisplaySettings* s = &settings;
    page.load = [s](const SerialValue& section) { *s = Graphics::LoadDisplay(section); };
    page.save = [s] { return Graphics::SaveDisplay(*s); };
    page.draw = [s, active = std::move(activePresentMode)](Slate::Context& ui, const Rect& r) {
        return DrawDisplayPage(ui, r, *s, active ? active() : std::string{});
    };
    return pages.Register(std::move(page));
}

RegistrationToken RegisterRenderingPage(SettingsPageRegistry& pages, Graphics::RenderingSettings& settings) {
    SettingsPageDesc page;
    page.id = std::string(Graphics::kRenderingPageId);
    page.category = "Engine";
    page.contentHeight = 508;
    page.title = "Rendering Quality";
    page.tooltip = "Quality tier, render scale, anti-aliasing, post effects, shadows, culling and texture "
                   "filtering (Config/Graphics.json).";
    page.owner = "Engine";
    page.configFile = std::string(Graphics::kConfigFile);
    Graphics::RenderingSettings* s = &settings;
    page.load = [s](const SerialValue& section) { *s = Graphics::LoadRendering(section); };
    page.save = [s] { return Graphics::SaveRendering(*s); };
    page.draw = [s](Slate::Context& ui, const Rect& r) { return DrawRenderingPage(ui, r, *s); };
    return pages.Register(std::move(page));
}

}  // namespace Luma::Editor::Panels
