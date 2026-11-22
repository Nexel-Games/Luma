#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Renderer/RenderQuality.h"
#include "Luma/Serialization/SerialValue.h"

// Per-project graphics settings: presentation / frame pacing and rendering
// quality. They live in the project's Config/Graphics.json, one section per
// Project Settings page:
//
//   Config/Graphics.json = {
//     "fileVersion": 1,
//     "Engine.Display":   { "vsync": false, "presentMode": "Mailbox", ... },
//     "Engine.Rendering": { "qualityTier": "High", "renderScale": 1.0, ... }
//   }
//
// The editor edits them through Project Settings (Engine > Display & Frame
// Rate, Engine > Rendering Quality) and applies changes live; the runtime
// (Sandbox) reads the same file at startup. Loading is tolerant: missing keys
// keep their defaults, unknown values fall back to defaults, numbers are
// clamped to their valid range (see Sanitize).

namespace Luma::Graphics {

inline constexpr std::string_view kConfigFile = "Graphics.json";
inline constexpr std::string_view kDisplayPageId = "Engine.Display";
inline constexpr std::string_view kRenderingPageId = "Engine.Rendering";

// --- Display & frame rate ------------------------------------------------------

struct DisplaySettings {
    // VSync: present in step with the monitor refresh (FIFO). Overrides
    // `presentMode`. Off by default so the editor is not capped at 60 FPS.
    bool vsync = false;
    // Swapchain present mode used when VSync is off.
    PresentMode presentMode = PresentMode::Mailbox;
    // Frame-rate cap in FPS (0 = uncapped).
    f32 frameRateLimit = 0.0f;
    // Cap while the window is unfocused or minimized (0 = same as the cap above).
    f32 backgroundFrameRateLimit = 30.0f;
    // Editor: when off the viewport only re-renders when the view or the scene
    // changes (plus a short settle time for auto exposure).
    bool realtimeViewport = true;

    bool operator==(const DisplaySettings&) const = default;
};

// The present mode to request: VSync forces FIFO.
PresentMode EffectivePresentMode(const DisplaySettings& display);
// The frame-rate cap for the current focus state (0 = none).
f32 EffectiveFrameRateLimit(const DisplaySettings& display, bool windowFocused);

// --- Rendering quality -----------------------------------------------------------

enum class QualityTier : u8 { Low, Medium, High, Epic, Custom };
enum class AntiAliasing : u8 { None, FXAA };

struct RenderingSettings {
    QualityTier tier = QualityTier::High;
    // Viewport resolution relative to its on-screen pixel size (1 = native).
    f32 renderScale = 1.0f;
    AntiAliasing antiAliasing = AntiAliasing::FXAA;
    bool bloom = true;
    bool autoExposure = true;
    // Sun cascaded shadows.
    bool shadows = true;
    u32 shadowMapSize = 2048;      // per cascade, power of two in [512, 4096]
    u32 maxShadowCascades = 4;     // [1, 4]
    f32 maxShadowDistance = 150.0f;  // caps each light's own shadow distance
    // Culling.
    bool frustumCulling = true;
    bool shadowCasterCulling = true;
    // Texture filtering (applied when the renderer starts: restart required).
    u32 maxAnisotropy = 16;      // 1, 2, 4, 8, 16
    f32 textureMipBias = 0.0f;   // [-2, 4]; positive = blurrier, cheaper

    bool operator==(const RenderingSettings&) const = default;
};

// Fields a quality tier sets (everything except culling and texture bias).
RenderingSettings TierPreset(QualityTier tier);
// Applies a tier's preset to `s` (Custom leaves the values as they are).
void ApplyTier(RenderingSettings& s, QualityTier tier);
// The tier whose preset matches `s` exactly, else Custom.
QualityTier MatchingTier(const RenderingSettings& s);

// Renderer-facing subset (what VulkanDeferredRenderer consumes).
Renderer2::RenderQuality ToRenderQuality(const RenderingSettings& s);

// --- Aggregate + persistence ----------------------------------------------------------

struct GraphicsSettings {
    DisplaySettings display;
    RenderingSettings rendering;

    bool operator==(const GraphicsSettings&) const = default;
};

// Clamps every field into its valid range (snaps shadow size / anisotropy).
void Sanitize(DisplaySettings& s);
void Sanitize(RenderingSettings& s);

SerialValue SaveDisplay(const DisplaySettings& s);
DisplaySettings LoadDisplay(const SerialValue& section);  // Null/missing -> defaults
SerialValue SaveRendering(const RenderingSettings& s);
RenderingSettings LoadRendering(const SerialValue& section);

// Reads <configDir>/Graphics.json (defaults when it does not exist). False
// with `error` when the file exists but is not a valid settings document; the
// settings are left at their defaults then.
bool LoadFromConfigDir(const std::filesystem::path& configDir, GraphicsSettings& out,
                       std::string* error = nullptr);

// Enum <-> stable strings (file format and UI labels).
std::string_view ToString(PresentMode mode);
std::string_view ToString(QualityTier tier);
std::string_view ToString(AntiAliasing aa);
bool FromString(std::string_view text, PresentMode& out);
bool FromString(std::string_view text, QualityTier& out);
bool FromString(std::string_view text, AntiAliasing& out);

}  // namespace Luma::Graphics
