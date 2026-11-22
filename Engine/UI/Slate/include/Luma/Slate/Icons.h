#pragma once

#include "Luma/Slate/Context.h"

namespace Luma::Slate {

// Upload the editor's packed artwork once per renderer, shared by all panels.
bool LoadIconAtlas(Context& ctx, Renderer& renderer, const std::string& path);
void DrawIcon(Context& ctx, const Rect& box, Icon icon,
              Color color = Color::RGB(255, 255, 255), f32 rotationRadians = 0.0f);

}  // namespace Luma::Slate
