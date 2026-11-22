#pragma once

#include <string>
#include <vector>

#include "Luma/Slate/Context.h"
#include "Luma/Slate/Theme.h"

namespace Luma::GraphEditor {

// A Slate Combo drawn on the black input-field background (like the
// numeric / text fields) instead of the button grey. Slate's Combo paints
// with the theme's button colours, so they are swapped for the call.
inline int DarkCombo(Slate::Context& ui, u64 id, const Slate::Rect& rect, const std::vector<std::string>& items,
                     int current) {
    Slate::Theme& t = ui.theme();
    const Slate::Color button = t.button, hover = t.buttonHover;
    t.button = t.fieldBg;
    t.buttonHover = Slate::Lighten(t.fieldBg, 0.08f);
    const int picked = ui.Combo(id, rect, items, current);
    t.button = button;
    t.buttonHover = hover;
    return picked;
}

}  // namespace Luma::GraphEditor
