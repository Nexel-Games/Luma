#include "Luma/Slate/Theme.h"

#include <algorithm>

namespace Luma::Slate {
namespace {
u8 ClampU8(f32 v) {
    return static_cast<u8>(std::clamp(v, 0.0f, 255.0f) + 0.5f);
}
}  // namespace

Color Lighten(Color c, f32 amount) {
    return Color{ClampU8(c.r + (255.0f - c.r) * amount),
                 ClampU8(c.g + (255.0f - c.g) * amount),
                 ClampU8(c.b + (255.0f - c.b) * amount), c.a};
}

Color Darken(Color c, f32 amount) {
    f32 k = 1.0f - amount;
    return Color{ClampU8(c.r * k), ClampU8(c.g * k), ClampU8(c.b * k), c.a};
}

Color Mix(Color a, Color b, f32 t) {
    return Color{ClampU8(a.r + (b.r - a.r) * t), ClampU8(a.g + (b.g - a.g) * t),
                 ClampU8(a.b + (b.b - a.b) * t), ClampU8(a.a + (b.a - a.a) * t)};
}

Theme DarkTheme() {
    Theme t;

    // Neutral surface ramp after Unreal's Starship editor palette
    // (SlateCore StyleColors): Background #151515 -> Panel #242424 ->
    // Header #2F2F2F -> Secondary/Dropdown #383838. Deep, low-chroma greys
    // keep the viewport the brightest thing on screen.
    t.surface0 = Color::RGB(21, 21, 21);   // window / title background
    t.surface1 = Color::RGB(36, 36, 36);   // panels
    t.surface2 = Color::RGB(47, 47, 47);   // panel headers / toolbars
    t.surface3 = Color::RGB(56, 56, 56);   // controls (button resting)
    t.surface4 = Color::RGB(71, 71, 71);   // raised overlays
    t.recessed = Color::RGB(26, 26, 26);   // inset wells (tree / lists)
    t.tabWell = Color::RGB(21, 21, 21);    // dock tab strip

    // Luma's orange identity stands in for Starship's blue Primary.
    t.accent = Color::RGB(255, 140, 0);
    t.accentHover = Color::RGB(255, 170, 50);
    t.accentActive = Color::RGB(220, 110, 0);
    t.accentMuted = Color::RGB(100, 55, 0);
    t.accentText = Color::RGB(255, 230, 200);

    // Semantic.
    t.separator = Color::RGB(15, 15, 15);  // Starship WindowBorder
    t.outline = Color::RGB(61, 61, 61);
    t.focusRing = t.accent;
    t.text = Color::RGB(204, 204, 204);    // Starship Foreground (+ legibility)
    t.textDim = Color::RGB(140, 140, 140);
    t.textDisabled = Color::RGB(92, 92, 92);
    // Selection = the accent, deep enough for white text on top.
    t.selectionBg = Color::RGB(186, 98, 8);
    t.selectionText = Color::RGB(255, 255, 255);
    t.success = Color::RGB(139, 194, 74);   // Starship AccentGreen
    t.warning = Color::RGB(255, 196, 46);
    t.error = Color::RGB(255, 72, 72);

    // Fields (search box, text fields): Starship Input / InputOutline.
    t.fieldBg = Color::RGB(15, 15, 15);
    t.fieldBorder = Color::RGB(56, 56, 56);
    // Text selection: the accent, translucent so the glyphs stay readable.
    t.textSelection = Color::RGBA(186, 98, 8, 140);
    t.caretWidth = 1.5f;
    t.fieldPadX = t.space.lg;
    t.checkGlyph = Color::RGB(255, 255, 255);
    t.checkGlyphWidth = 2.0f;
    t.spinFill = Color::RGB(48, 48, 48);
    t.spinFillHover = Color::RGB(62, 62, 62);
    t.dragThreshold = 4.0f;

    // Legacy flat fields mapped onto the ramp (keeps existing widgets working).
    t.windowBg = t.surface0;
    t.panelBg = t.surface1;
    t.panelBorder = t.separator;
    t.header = t.surface2;
    t.button = t.surface3;
    t.buttonHover = Color::RGB(87, 87, 87);   // Starship Hover
    t.buttonActive = Color::RGB(42, 42, 42);
    t.buttonText = Color::RGB(214, 214, 214);
    t.cardBg = t.surface2;
    t.cardHover = t.surface3;
    t.cardSelected = t.accentMuted;
    t.caret = t.text;
    t.rounding = t.radius.md;

    // Menus (Starship "Menu.*"): Dropdown fill + DropdownOutline border,
    // White25 headings/separators, Primary (here: Luma orange) hover rows.
    t.menu.background = Color::RGB(56, 56, 56);
    t.menu.outline = Color::RGB(76, 76, 76);
    t.menu.separator = Color::RGBA(255, 255, 255, 34);
    t.menu.headingText = Color::RGBA(255, 255, 255, 118);
    t.menu.shortcutText = Color::RGB(136, 136, 136);
    t.menu.iconTint = Color::RGB(192, 192, 192);
    t.menu.hoverFill = Color::RGB(87, 87, 87);
    t.menu.focusFill = Color::RGB(74, 74, 74);
    t.menu.highlightFill = t.selectionBg;
    t.menu.highlightText = Color::RGB(255, 255, 255);

    return t;
}

}  // namespace Luma::Slate
