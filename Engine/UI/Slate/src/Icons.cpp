#include "Luma/Slate/Icons.h"

#include <algorithm>
#include <mutex>
#include <unordered_set>
#include <iterator>
#include <string_view>

namespace Luma::Slate {
namespace {
constexpr u32 kSlots = static_cast<u32>(Icon::Count) - 1;

constexpr bool IsFullColor(std::string_view source) { return source.starts_with("Luma_Icons/"); }

// Per slot: true for Luma icon pack artwork (drawn in its own colours).
constexpr bool kFullColor[] = {
#define LUMA_ICON(name, source) IsFullColor(source),
#include "Luma/Slate/IconSources.inc"
#undef LUMA_ICON
};
static_assert(std::size(kFullColor) == kSlots, "IconSources.inc must list every icon slot");
}  // namespace

namespace {
std::mutex g_fullColorMutex;
std::unordered_set<TextureHandle>& FullColorTextures() {
    static std::unordered_set<TextureHandle> set;
    return set;
}
}  // namespace

void MarkFullColorTexture(TextureHandle texture) {
    if (!texture) return;
    std::scoped_lock lock(g_fullColorMutex);
    FullColorTextures().insert(texture);
}

void UnmarkFullColorTexture(TextureHandle texture) {
    std::scoped_lock lock(g_fullColorMutex);
    FullColorTextures().erase(texture);
}

bool IsFullColorTexture(TextureHandle texture) {
    if (!texture) return false;
    std::scoped_lock lock(g_fullColorMutex);
    return FullColorTextures().contains(texture);
}

bool IsLumaIconPath(std::string_view path) {
    return path.find("Luma_Icons/") != std::string_view::npos ||
           path.find("Luma_Icons\\") != std::string_view::npos;
}

bool IconIsFullColor(Icon icon) {
    return icon > Icon::None && icon < Icon::Count && kFullColor[static_cast<u32>(icon) - 1];
}

bool LoadIconAtlas(Context& ctx, Renderer& renderer, const std::string& path) {
    u32 width = 0, height = 0;
    std::vector<u8> pixels;
    if (path.empty() || !LoadImagePixels(path, width, height, pixels) ||
        height == 0 || width != kSlots * height) return false;
    const auto texture = renderer.CreateTexture(width, height, pixels.data());
    if (!texture) return false;
    ctx.SetIconAtlas(texture);
    return true;
}

void DrawIcon(Context& ctx, const Rect& box, Icon icon, Color color, f32 rotationRadians) {
    if (!ctx.IconAtlas() || icon <= Icon::None || icon >= Icon::Count) return;
    const f32 side = std::min(box.w, box.h);
    if (side <= 0) return;
    constexpr f32 slotWidth = 1.0f / kSlots;
    const f32 u = static_cast<f32>(static_cast<u32>(icon) - 1) * slotWidth;
    const Rect uv{u, 0, slotWidth, 1};
    // Full-colour artwork keeps its colours; the tint only fades it.
    if (IconIsFullColor(icon)) color = Color::RGBA(255, 255, 255, color.a);
    if (rotationRadians != 0) {
        ctx.drawList().AddImageRotated(ctx.IconAtlas(), {box.x + box.w / 2, box.y + box.h / 2},
                                      side / 2, rotationRadians, color, uv);
        return;
    }
    ctx.ImageUV(ctx.IconAtlas(), {box.x + (box.w - side) * 0.5f, box.y + (box.h - side) * 0.5f, side, side},
                uv, color);
}
}  // namespace Luma::Slate
