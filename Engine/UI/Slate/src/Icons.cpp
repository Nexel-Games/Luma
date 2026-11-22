#include "Luma/Slate/Icons.h"

#include <algorithm>

namespace Luma::Slate {
namespace {
constexpr u32 kSlots = static_cast<u32>(Icon::Count) - 1;
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
    if (rotationRadians != 0) {
        ctx.drawList().AddImageRotated(ctx.IconAtlas(), {box.x + box.w / 2, box.y + box.h / 2},
                                      side / 2, rotationRadians, color, uv);
        return;
    }
    ctx.ImageUV(ctx.IconAtlas(), {box.x + (box.w - side) * 0.5f, box.y + (box.h - side) * 0.5f, side, side},
                uv, color);
}
}  // namespace Luma::Slate
