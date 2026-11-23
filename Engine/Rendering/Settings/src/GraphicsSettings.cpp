#include "Luma/Graphics/GraphicsSettings.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include "Luma/Serialization/Json.h"

namespace Luma::Graphics {

namespace {

constexpr int kFileVersion = 1;

f32 ClampFinite(f32 v, f32 lo, f32 hi, f32 fallback) {
    if (!std::isfinite(v)) return fallback;
    return std::clamp(v, lo, hi);
}

// Nearest power of two in [lo, hi].
u32 SnapPow2(u32 v, u32 lo, u32 hi) {
    v = std::clamp(v, lo, hi);
    u32 best = lo;
    for (u32 p = lo; p <= hi; p *= 2) {
        const u32 dBest = v > best ? v - best : best - v;
        const u32 dP = v > p ? v - p : p - v;
        if (dP < dBest) best = p;
    }
    return best;
}

bool ReadBool(const SerialValue& section, std::string_view key, bool def) {
    const SerialValue* v = section.IsObject() ? section.Find(key) : nullptr;
    return v ? v->AsBool(def) : def;
}
f32 ReadFloat(const SerialValue& section, std::string_view key, f32 def) {
    const SerialValue* v = section.IsObject() ? section.Find(key) : nullptr;
    return v ? static_cast<f32>(v->AsFloat(def)) : def;
}
u32 ReadU32(const SerialValue& section, std::string_view key, u32 def) {
    const SerialValue* v = section.IsObject() ? section.Find(key) : nullptr;
    if (!v) return def;
    const i64 n = v->AsInt(static_cast<i64>(def));
    return n < 0 ? 0u : static_cast<u32>(std::min<i64>(n, 1 << 20));
}
template <typename E>
E ReadEnum(const SerialValue& section, std::string_view key, E def) {
    const SerialValue* v = section.IsObject() ? section.Find(key) : nullptr;
    if (!v) return def;
    E out = def;
    return FromString(v->AsString(), out) ? out : def;
}

}  // namespace

// --- Enums -----------------------------------------------------------------------------

std::string_view ToString(PresentMode mode) {
    switch (mode) {
        case PresentMode::Fifo: return "Fifo";
        case PresentMode::Mailbox: return "Mailbox";
        case PresentMode::Immediate: return "Immediate";
    }
    return "Fifo";
}
std::string_view ToString(QualityTier tier) {
    switch (tier) {
        case QualityTier::Low: return "Low";
        case QualityTier::Medium: return "Medium";
        case QualityTier::High: return "High";
        case QualityTier::Epic: return "Epic";
        case QualityTier::Custom: return "Custom";
    }
    return "Custom";
}
std::string_view ToString(AntiAliasing aa) { return aa == AntiAliasing::FXAA ? "FXAA" : "None"; }

bool FromString(std::string_view text, PresentMode& out) {
    for (PresentMode m : {PresentMode::Fifo, PresentMode::Mailbox, PresentMode::Immediate}) {
        if (text == ToString(m)) {
            out = m;
            return true;
        }
    }
    return false;
}
bool FromString(std::string_view text, QualityTier& out) {
    for (QualityTier t :
         {QualityTier::Low, QualityTier::Medium, QualityTier::High, QualityTier::Epic, QualityTier::Custom}) {
        if (text == ToString(t)) {
            out = t;
            return true;
        }
    }
    return false;
}
bool FromString(std::string_view text, AntiAliasing& out) {
    for (AntiAliasing a : {AntiAliasing::None, AntiAliasing::FXAA}) {
        if (text == ToString(a)) {
            out = a;
            return true;
        }
    }
    return false;
}

// --- Display ---------------------------------------------------------------------------

PresentMode EffectivePresentMode(const DisplaySettings& display) {
    return display.vsync ? PresentMode::Fifo : display.presentMode;
}

f32 EffectiveFrameRateLimit(const DisplaySettings& display, bool windowFocused) {
    const f32 cap = display.frameRateLimit;
    if (windowFocused || display.backgroundFrameRateLimit <= 0.0f) return cap;
    // The background cap only ever lowers the rate.
    return cap > 0.0f ? std::min(cap, display.backgroundFrameRateLimit) : display.backgroundFrameRateLimit;
}

void Sanitize(DisplaySettings& s) {
    const DisplaySettings d;
    s.frameRateLimit = ClampFinite(s.frameRateLimit, 0.0f, 1000.0f, d.frameRateLimit);
    s.backgroundFrameRateLimit = ClampFinite(s.backgroundFrameRateLimit, 0.0f, 1000.0f, d.backgroundFrameRateLimit);
    // Caps below 5 FPS make the editor unusable; treat them as "off".
    if (s.frameRateLimit > 0.0f && s.frameRateLimit < 5.0f) s.frameRateLimit = 5.0f;
    if (s.backgroundFrameRateLimit > 0.0f && s.backgroundFrameRateLimit < 5.0f) s.backgroundFrameRateLimit = 5.0f;
    if (static_cast<u8>(s.presentMode) > static_cast<u8>(PresentMode::Immediate)) s.presentMode = d.presentMode;
}

SerialValue SaveDisplay(const DisplaySettings& s) {
    SerialValue out = SerialValue::MakeObject();
    out["vsync"] = s.vsync;
    out["presentMode"] = std::string(ToString(s.presentMode));
    out["frameRateLimit"] = static_cast<f64>(s.frameRateLimit);
    out["backgroundFrameRateLimit"] = static_cast<f64>(s.backgroundFrameRateLimit);
    out["realtimeViewport"] = s.realtimeViewport;
    return out;
}

DisplaySettings LoadDisplay(const SerialValue& section) {
    DisplaySettings s;
    s.vsync = ReadBool(section, "vsync", s.vsync);
    s.presentMode = ReadEnum(section, "presentMode", s.presentMode);
    s.frameRateLimit = ReadFloat(section, "frameRateLimit", s.frameRateLimit);
    s.backgroundFrameRateLimit = ReadFloat(section, "backgroundFrameRateLimit", s.backgroundFrameRateLimit);
    s.realtimeViewport = ReadBool(section, "realtimeViewport", s.realtimeViewport);
    Sanitize(s);
    return s;
}

// --- Rendering quality -------------------------------------------------------------------

RenderingSettings TierPreset(QualityTier tier) {
    RenderingSettings s;  // High (the defaults)
    s.tier = tier;
    switch (tier) {
        case QualityTier::Low:
            s.renderScale = 0.75f;
            s.antiAliasing = AntiAliasing::None;
            s.bloom = false;
            s.autoExposure = true;
            s.shadows = true;
            s.shadowMapSize = 1024;
            s.maxShadowCascades = 2;
            s.maxShadowDistance = 50.0f;
            s.maxAnisotropy = 4;
            break;
        case QualityTier::Medium:
            s.renderScale = 1.0f;
            s.antiAliasing = AntiAliasing::FXAA;
            s.bloom = true;
            s.shadowMapSize = 1024;
            s.maxShadowCascades = 3;
            s.maxShadowDistance = 80.0f;
            s.maxAnisotropy = 8;
            break;
        case QualityTier::High:
        case QualityTier::Custom:
            break;
        case QualityTier::Epic:
            s.renderScale = 1.5f;
            s.shadowMapSize = 4096;
            s.maxShadowCascades = 4;
            s.maxShadowDistance = 300.0f;
            break;
    }
    return s;
}

void ApplyTier(RenderingSettings& s, QualityTier tier) {
    if (tier == QualityTier::Custom) {
        s.tier = QualityTier::Custom;
        return;
    }
    const RenderingSettings p = TierPreset(tier);
    s.tier = tier;
    s.renderScale = p.renderScale;
    s.antiAliasing = p.antiAliasing;
    s.bloom = p.bloom;
    s.autoExposure = p.autoExposure;
    s.shadows = p.shadows;
    s.shadowMapSize = p.shadowMapSize;
    s.maxShadowCascades = p.maxShadowCascades;
    s.maxShadowDistance = p.maxShadowDistance;
    s.maxAnisotropy = p.maxAnisotropy;
}

QualityTier MatchingTier(const RenderingSettings& s) {
    for (QualityTier t : {QualityTier::Low, QualityTier::Medium, QualityTier::High, QualityTier::Epic}) {
        // Apply the preset over a copy: fields the tier doesn't set stay
        // equal, so the copy equals `s` exactly when the tier fields match.
        RenderingSettings candidate = s;
        ApplyTier(candidate, t);
        candidate.tier = s.tier;
        if (candidate == s) return t;
    }
    return QualityTier::Custom;
}

Renderer2::RenderQuality ToRenderQuality(const RenderingSettings& s) {
    Renderer2::RenderQuality q;
    q.shadows = s.shadows;
    q.shadowMapSize = s.shadowMapSize;
    q.maxCascades = s.maxShadowCascades;
    q.maxShadowDistance = s.maxShadowDistance;
    q.fxaa = s.antiAliasing == AntiAliasing::FXAA;
    q.bloom = s.bloom;
    q.autoExposure = s.autoExposure;
    q.frustumCulling = s.frustumCulling;
    q.shadowCasterCulling = s.shadowCasterCulling;
    q.meshLods = s.meshLods;
    q.lodBias = s.lodBias;
    return q;
}

void Sanitize(RenderingSettings& s) {
    const RenderingSettings d;
    s.renderScale = ClampFinite(s.renderScale, 0.25f, 2.0f, d.renderScale);
    s.shadowMapSize = SnapPow2(s.shadowMapSize, 512, 4096);
    s.maxShadowCascades = std::clamp(s.maxShadowCascades, 1u, 4u);
    s.maxShadowDistance = ClampFinite(s.maxShadowDistance, 5.0f, 2000.0f, d.maxShadowDistance);
    s.maxAnisotropy = SnapPow2(std::max(s.maxAnisotropy, 1u), 1, 16);
    s.textureMipBias = ClampFinite(s.textureMipBias, -2.0f, 4.0f, d.textureMipBias);
    s.lodBias = std::clamp(s.lodBias, -2, 3);
    if (static_cast<u8>(s.tier) > static_cast<u8>(QualityTier::Custom)) s.tier = QualityTier::Custom;
    if (static_cast<u8>(s.antiAliasing) > static_cast<u8>(AntiAliasing::FXAA)) s.antiAliasing = d.antiAliasing;
}

SerialValue SaveRendering(const RenderingSettings& s) {
    SerialValue out = SerialValue::MakeObject();
    out["qualityTier"] = std::string(ToString(s.tier));
    out["renderScale"] = static_cast<f64>(s.renderScale);
    out["antiAliasing"] = std::string(ToString(s.antiAliasing));
    out["bloom"] = s.bloom;
    out["autoExposure"] = s.autoExposure;
    out["shadows"] = s.shadows;
    out["shadowMapSize"] = static_cast<i64>(s.shadowMapSize);
    out["maxShadowCascades"] = static_cast<i64>(s.maxShadowCascades);
    out["maxShadowDistance"] = static_cast<f64>(s.maxShadowDistance);
    out["frustumCulling"] = s.frustumCulling;
    out["shadowCasterCulling"] = s.shadowCasterCulling;
    out["meshLods"] = s.meshLods;
    out["lodBias"] = static_cast<i64>(s.lodBias);
    out["maxAnisotropy"] = static_cast<i64>(s.maxAnisotropy);
    out["textureMipBias"] = static_cast<f64>(s.textureMipBias);
    return out;
}

RenderingSettings LoadRendering(const SerialValue& section) {
    RenderingSettings s;
    s.tier = ReadEnum(section, "qualityTier", s.tier);
    s.renderScale = ReadFloat(section, "renderScale", s.renderScale);
    s.antiAliasing = ReadEnum(section, "antiAliasing", s.antiAliasing);
    s.bloom = ReadBool(section, "bloom", s.bloom);
    s.autoExposure = ReadBool(section, "autoExposure", s.autoExposure);
    s.shadows = ReadBool(section, "shadows", s.shadows);
    s.shadowMapSize = ReadU32(section, "shadowMapSize", s.shadowMapSize);
    s.maxShadowCascades = ReadU32(section, "maxShadowCascades", s.maxShadowCascades);
    s.maxShadowDistance = ReadFloat(section, "maxShadowDistance", s.maxShadowDistance);
    s.frustumCulling = ReadBool(section, "frustumCulling", s.frustumCulling);
    s.shadowCasterCulling = ReadBool(section, "shadowCasterCulling", s.shadowCasterCulling);
    s.meshLods = ReadBool(section, "meshLods", s.meshLods);
    if (const SerialValue* v = section.IsObject() ? section.Find("lodBias") : nullptr) {
        s.lodBias = static_cast<i32>(std::clamp<i64>(v->AsInt(s.lodBias), -2, 3));
    }
    s.maxAnisotropy = ReadU32(section, "maxAnisotropy", s.maxAnisotropy);
    s.textureMipBias = ReadFloat(section, "textureMipBias", s.textureMipBias);
    Sanitize(s);
    // The stored tier is only a label: the values are authoritative, so a file
    // edited by hand shows "Custom" when they no longer match the preset.
    if (s.tier != QualityTier::Custom && MatchingTier(s) != s.tier) s.tier = QualityTier::Custom;
    return s;
}

bool LoadFromConfigDir(const std::filesystem::path& configDir, GraphicsSettings& out, std::string* error) {
    out = GraphicsSettings{};
    const std::filesystem::path file = configDir / kConfigFile;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return true;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error) *error = "could not read " + file.string();
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string parseError;
    auto doc = ParseJson(text, &parseError);
    if (!doc || !doc->IsObject()) {
        if (error) *error = file.string() + " is not a valid settings file (" +
                            (doc ? std::string("not a JSON object") : parseError) + ")";
        return false;
    }
    if (const SerialValue* v = doc->Find("fileVersion"); v && v->AsInt() > kFileVersion) {
        if (error) *error = file.string() + " was written by a newer engine";
        return false;
    }
    const SerialValue* display = doc->Find(kDisplayPageId);
    const SerialValue* rendering = doc->Find(kRenderingPageId);
    out.display = LoadDisplay(display ? *display : SerialValue{});
    out.rendering = LoadRendering(rendering ? *rendering : SerialValue{});
    return true;
}

}  // namespace Luma::Graphics
