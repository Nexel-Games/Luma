#pragma once

#include "Luma/Slate/Context.h"

namespace Luma::Slate {

// Upload the editor's packed artwork once per renderer, shared by all panels.
bool LoadIconAtlas(Context& ctx, Renderer& renderer, const std::string& path);
// Full-colour textures (Luma icon pack artwork loaded as its own texture):
// Context::Image applies only the tint's alpha to them, so panels that tint
// monochrome icons (dim at rest, accent when active) don't darken them.
// Process-wide; unmark before destroying a texture whose handle may be reused.
void MarkFullColorTexture(TextureHandle texture);
void UnmarkFullColorTexture(TextureHandle texture);
bool IsFullColorTexture(TextureHandle texture);
// True for a path inside a Luma icon pack (".../Luma_Icons/..." or ".../Luma_Icons_V2/...").
bool IsLumaIconPath(std::string_view path);

// True for slots drawn from the Luma icon pack (multi-colour): DrawIcon ignores
// the tint's RGB for them and applies only its alpha.
bool IconIsFullColor(Icon icon);
void DrawIcon(Context& ctx, const Rect& box, Icon icon,
              Color color = Color::RGB(255, 255, 255), f32 rotationRadians = 0.0f);
// The Add "+" for Add / Create buttons: a bold vector cross filling `box`
// (the pack's Plus art is padded and reads as a speck at button sizes).
void DrawAddGlyph(Context& ctx, const Rect& box, Color color);

}  // namespace Luma::Slate
