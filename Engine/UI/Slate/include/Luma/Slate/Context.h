#pragma once

#include <bitset>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Luma/Platform/Cursor.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Slate/DragDrop.h"
#include "Luma/Slate/DrawList.h"
#include "Luma/Slate/Font.h"
#include "Luma/Slate/IconKind.h"
#include "Luma/Slate/Image.h"
#include "Luma/Slate/Menu.h"
#include "Luma/Slate/TextEditState.h"
#include "Luma/Slate/Theme.h"
#include "Luma/Slate/Types.h"

// Luma Slate immediate-mode UI context. The app feeds input each frame, then
// calls widget functions between BeginFrame/EndFrame; EndFrame returns the
// UIDrawData to hand to Renderer::DrawUI.
//
// Visual constants (colors, sizes, radii, motion) live in Theme.h. The
// Context owns one Theme that widgets read through m_theme.

namespace Luma::Slate {

// How a text edit ended (mirrors the usual editor commit reasons).
enum class TextCommit {
    None,         // still editing (or not focused)
    OnEnter,      // Enter pressed
    OnFocusLost,  // clicked elsewhere / Tab moved focus / focus taken
    OnCleared,    // Escape: the text was reverted to its focus-start value
};

struct TextFieldOptions {
    std::string_view placeholder;
    bool selectAllOnFocus = false;  // select everything when focus arrives by click
    bool readOnly = false;          // caret/selection/copy only
    usize maxLength = 0;            // bytes; 0 = unlimited
    bool revertOnEscape = true;     // Escape reverts and commits OnCleared
};

struct TextResult {
    bool changed = false;           // text differs from last frame
    TextCommit commit = TextCommit::None;
};

// Unreal-style numeric entry (SpinBox / SpinBoxInt / VectorField).
struct SpinBoxOptions {
    std::optional<f64> min, max;              // hard limits (clamp)
    std::optional<f64> sliderMin, sliderMax;  // range shown as a fill bar; drags map over it
    f64 delta = 0.0;                          // snap step (0 = continuous)
    f64 speed = 0.02;                         // value per pixel dragged (no slider range)
    f32 shiftMultiplier = 10.0f;              // drag faster while Shift is held
    f32 ctrlMultiplier = 0.1f;                // drag finer while Ctrl is held
    f32 sliderExponent = 1.0f;                // non-linear drag over the slider range
    int minFractionalDigits = 1;
    int maxFractionalDigits = 3;
    bool enableWheel = false;                 // mouse wheel steps the value when hovered
    std::string_view suffix;                  // unit text appended verbatim, e.g. " cm", "°"
    const Color* accent = nullptr;            // axis stripe at the left edge (X/Y/Z)
};

struct NumericResult {
    bool changed = false;      // the bound value changed this frame
    bool committed = false;    // a typed value was committed (Enter / focus loss)
    bool dragStarted = false;  // a scrub began (undo systems open a transaction)
    bool dragEnded = false;    // a scrub ended (... and close it)
};

// Checkbox state; Undetermined shows a dash (mixed values in a multi-selection).
enum class CheckState { Unchecked, Checked, Undetermined };

// Horizontal text alignment for LabelIn.
enum class Align { Left, Center, Right };

class Context {
public:
    // Loads the fonts through the renderer. Empty paths fall back to the
    // previous font in the chain: title -> body, ui -> title, medium -> ui,
    // mono -> body. Returns false on error. Init must be called before any
    // widget draws.
    bool Init(Renderer& renderer, const std::string& fontPath,
              const std::string& titleFontPath = {}, f32 baseSize = 16.0f,
              f32 titleSize = 30.0f, const std::string& uiFontPath = {},
              f32 uiSize = 15.0f, const std::string& mediumFontPath = {},
              f32 mediumSize = 12.5f,
              const std::string& monoFontPath = {}, f32 monoSize = 13.0f,
              f32 dpiScale = 1.0f);

    // Preferred entry: load the font chain from a Theme::Typography struct so
    // weights, sizes, and family paths are all driven by the design system.
    // Empty string fields fall back to the previous weight in the chain
    // (semiBold -> medium -> regular). `dpiScale` (default 1.0) multiplies
    // every pixel size in `type` before baking the atlases so the product
    // stays crisp on hi-DPI displays without changing the design tokens.
    bool Init(Renderer& renderer, const Typography& type, f32 dpiScale = 1.0f);

    // --- Input feed (call from the app's event callback) --------------------
    void OnMouseMove(f32 x, f32 y);
    // Moves the pointer without producing motion (after the app warps the
    // OS cursor, e.g. at the end of a pointer-locked drag).
    void WarpMouse(f32 x, f32 y) { m_mouse = m_prevMouse = {x, y}; }
    void OnMouseButton(int button, bool down);  // 0=left,1=right,2=middle
    void OnScroll(f32 y);
    void OnText(u32 codepoint);
    void OnKey(int glfwKey, bool down);

    // --- Platform services (the app wires these to its window) -------------
    // Clipboard: with no handlers set, an internal store is used (tests).
    using ClipboardGet = std::function<std::string()>;
    using ClipboardSet = std::function<void(const std::string&)>;
    void SetClipboardHandlers(ClipboardGet get, ClipboardSet set);
    std::string ClipboardText() const;
    void SetClipboardText(const std::string& text);
    // Pointer lock (hidden, unbounded pointer) for scrubbing drags. A widget
    // calls RequestPointerLock(true) every frame it wants the lock; the
    // request resets at BeginFrame. When a drag ends the widget sets a
    // restore point with RequestPointerRestore; the app moves the cursor
    // there (TakePointerRestore) and unlocks.
    void RequestPointerLock(bool locked) { m_pointerLock = m_pointerLock || locked; }
    bool PointerLockRequested() const { return m_pointerLock; }
    void RequestPointerRestore(Vec2 pos) { m_pointerRestore = pos; }
    std::optional<Vec2> TakePointerRestore() {
        auto p = m_pointerRestore;
        m_pointerRestore.reset();
        return p;
    }

    // --- Frame --------------------------------------------------------------
    void BeginFrame(f32 displayWidth, f32 displayHeight, f32 dt);
    const UIDrawData& EndFrame();

    // Animates 0..1 toward `active ? 1 : 0` with an exponential lerp at
    // `speed` per second. State is keyed by `id` so multiple independent
    // animations can coexist (hover fades, popup reveals, etc.). Returns
    // the current value. Each frame, ids that haven't been touched in
    // kAnimGcAge frames are evicted from the map.
    f32 Animate(u64 id, bool active, f32 speed);

    // --- Widgets ------------------------------------------------------------
    void Panel(const Rect& rect, Color color);
    void PanelBordered(const Rect& rect, Color fill, Color border,
                       f32 thickness = 1.0f);
    void PanelRounded(const Rect& rect, Color color, f32 radius);
    void PanelRoundedBordered(const Rect& rect, Color fill, Color border,
                              f32 radius, f32 thickness = 1.0f);
    void GradientRect(const Rect& rect, Color top, Color bottom);
    void Triangle(Vec2 a, Vec2 b, Vec2 c, Color color);
    void Image(TextureHandle texture, const Rect& rect,
               Color tint = Color::RGB(255, 255, 255));
    void PushClip(const Rect& rect);
    void PopClip();
    void ImageUV(TextureHandle texture, const Rect& rect, const Rect& uv,
                 Color tint = Color::RGB(255, 255, 255));
    // The renderer owns the icon texture for the lifetime of this context.
    void SetBrandIcon(const Slate::Image& icon) { m_brandIcon = icon; }
    void SetIconAtlas(TextureHandle texture) { m_iconAtlas = texture; }
    TextureHandle IconAtlas() const { return m_iconAtlas; }
    void LogoMark(Vec2 center, f32 radius);
    void Label(Vec2 pos, std::string_view text, Color color);
    void LabelIn(const Rect& rect, std::string_view text, Color color,
                 Align align = Align::Left, bool title = false);
    // Label rendered with the monospace font (console / logs / code).
    void LabelInMono(const Rect& rect, std::string_view text, Color color,
                     Align align = Align::Left);
    // Draws text in the semibold UI font (for tab titles, section headers).
    void Heading(const Rect& rect, std::string_view text, Color color,
                 Align align = Align::Left);
    bool Button(u64 id, const Rect& rect, std::string_view label);
    // A flat menu-bar item: text only, with a rounded highlight on hover/press.
    bool MenuButton(u64 id, const Rect& rect, std::string_view label);
    // Section header inside popup menus (Unreal-style): muted text with a
    // hairline divider above. Not clickable, no hover state; the caller adds
    // the vertical gap above the rect (see Theme::menu.sectionGap).
    void MenuSectionHeader(const Rect& rect, std::string_view label);
    bool Tab(u64 id, const Rect& rect, std::string_view label, bool active);
    bool TextField(u64 id, const Rect& rect, std::string& text,
                   std::string_view placeholder = {});
    // The full single-line text field: click to place the caret, drag /
    // Shift+click / double-click to select, Ctrl+A/C/X/V/Z/Y, Ctrl+arrows
    // and Ctrl+Backspace/Delete by word, Home/End, Enter commits, Escape
    // reverts, clicking elsewhere or Tab commits OnFocusLost. The caller's
    // `text` is updated live while typing.
    TextResult EditableText(u64 id, const Rect& rect, std::string& text,
                            const TextFieldOptions& options = {});
    // Keyboard focus chain: focusable widgets register in draw order every
    // frame (skipped while input is blocked, so an open menu / modal keeps
    // Tab to its own widgets). Tab / Shift+Tab move focus to the next /
    // previous registered widget, wrapping.
    void RegisterFocusable(u64 id, bool textEntry);
    // True during the frame after `id` received focus through Tab.
    bool FocusedByTab(u64 id) const { return id != 0 && m_tabFocusId == id; }
    // Gives keyboard focus to a non-text widget (e.g. a clicked checkbox).
    void FocusWidget(u64 id) { SetFocus(id, false); }
    bool Card(u64 id, const Rect& rect, std::string_view title,
              std::string_view desc, bool selected);
    // A selectable card whose content is a single image (e.g. a template
    // thumbnail); shows a selection/hover border.
    bool ImageCard(u64 id, const Rect& rect, TextureHandle image, bool selected);
    // A button showing a centered icon.
    bool IconButton(u64 id, const Rect& rect, TextureHandle icon);
    bool Checkbox(u64 id, const Rect& box, std::string_view label, bool& value);
    // Tri-state checkbox: a click on Undetermined checks it, then toggles.
    // Focusable with Tab; Space toggles while it has focus.
    bool Checkbox(u64 id, const Rect& box, std::string_view label, CheckState& state);
    // A numeric field you scrub by dragging horizontally. Returns true if the
    // value changed this frame. When `accent` is non-null, a small colored
    // square (Unreal-style channel marker) is drawn at the field's left edge
    // and the value reads left-aligned after it (no letter label).
    bool DragFloat(u64 id, const Rect& rect, f32& value, f32 speed = 0.02f,
                   const Color* accent = nullptr);
    // Unreal-style numeric field. Drag to scrub (Shift faster, Ctrl finer;
    // the pointer is hidden and locked while scrubbing), click without
    // dragging to type — math expressions such as "2*3.5" are evaluated;
    // Enter / focus loss commits, Escape reverts. Values clamp to min/max
    // and snap to delta. A slider range draws a fill bar and maps drags
    // across the field width.
    NumericResult SpinBox(u64 id, const Rect& rect, f32& value, const SpinBoxOptions& options = {});
    NumericResult SpinBoxInt(u64 id, const Rect& rect, i32& value,
                             const SpinBoxOptions& options = {});
    // 2-4 SpinBox cells side by side (X red, Y green, Z blue, W grey
    // stripes) editing values[0..count-1]. Results are OR'ed across cells.
    NumericResult VectorField(u64 id, const Rect& rect, f32* values, int count,
                              const SpinBoxOptions& options = {});
    // Three drag fields with colored X/Y/Z labels (edits xyz[0..2] in place).
    bool Vector3Field(u64 id, const Rect& rect, f32* xyz);
    // A collapsible section header; toggles `open` when clicked.
    bool CollapsingHeader(u64 id, const Rect& rect, std::string_view label,
                          bool& open);

    // --- New widget catalog ------------------------------------------------
    // Toggle (animated switch). Returns true if value changed.
    bool Toggle(u64 id, const Rect& rect, bool& value);
    // Slider with [min..max] range. Returns true if value changed.
    bool Slider(u64 id, const Rect& rect, f32& value, f32 min, f32 max);
    // Dropdown. Returns the new index (or `current` if unchanged / closed).
    // `items` is a list of labels; `current` is the selected index. The popup
    // opens on click, closes on selection or outside-click.
    int Dropdown(u64 id, const Rect& rect,
                 const std::vector<std::string>& items, int current);
    // Hierarchical tree node. Returns true if the open state toggled.
    bool TreeNode(u64 id, const Rect& rect, std::string_view label, Icon icon,
                  bool& open, int depth = 0, bool selected = false);
    // Inspector-style property row: label column on the left, returns the
    // field rect on the right so the caller can place a control inside it.
    // `labelWidth` is the label column width in px.
    Rect PropertyRow(const Rect& rect, std::string_view label,
                     f32 labelWidth = 120.0f);
    // Input + search icon + clear-X when text is non-empty. Returns true if
    // text changed (typing or clearing).
    bool SearchBox(u64 id, const Rect& rect, std::string& text,
                   std::string_view placeholder = "Search...");
    // Same as above, but uses the supplied texture as the leading icon
    // instead of the pack Search glyph. Falls back to the glyph
    // when texture == 0.
    bool SearchBox(u64 id, const Rect& rect, std::string& text,
                   TextureHandle leadingIcon,
                   std::string_view placeholder = "Search...");
    // Vertical scrollbar overlay for a content region. `viewport` is the
    // visible area; `contentHeight` is the total content height in the same
    // coordinate space. `scroll` is the current offset (px); the caller
    // persists it and draws its content shifted up by the returned value.
    // Returns the clamped offset — 0 when the content fits (and draws
    // nothing). Wheel over the viewport scrolls, the thumb drags, clicking
    // the track jumps. The bar fades in on hover, Unreal-style.
    f32 VerticalScroll(u64 id, const Rect& viewport, f32 contentHeight,
                       f32 scroll);
    // Modal popup menu. Returns the clicked item index, or -1 if dismissed.
    // Closes on selection or outside-click; backdrop dims the rest of the UI.
    struct MenuItem {
        std::string label;
        Icon icon = Icon::None;
        bool enabled = true;
        bool separatorAfter = false;
    };
    int MenuPopup(u64 id, const Rect& anchor,
                  const std::vector<MenuItem>& items);
    // Hover tooltip (Unreal-style). Call next to the control it describes:
    // while the pointer rests on `anchor` for kTooltipDelaySec (no button
    // held) the tip appears near the pointer, drawn by EndFrame on top of
    // everything. The first line of `text` is the title; lines after '\n'
    // are a dimmer description (word-wrapped). `id` identifies the control
    // (the text-only overload hashes the text).
    void Tooltip(u64 id, const Rect& anchor, std::string_view text);
    void Tooltip(const Rect& anchor, std::string_view text);
    // Modal dialog. BeginModal centers `title`-bar window at given size and
    // dims the rest of the UI; returns the content rect. EndModal closes.
    // ModalButtonRow lays out OK / Cancel at the bottom (returns true for OK).
    struct ModalResult { bool ok = false; bool cancel = false; bool open = true; };
    Rect BeginModal(u64 id, std::string_view title, const Vec2& size);
    void EndModal();
    ModalResult ModalButtonRow(u64 id, const Rect& content,
                               std::string_view okLabel = "OK",
                               std::string_view cancelLabel = "Cancel");
    // --- Drag and drop (see DragDrop.h) -------------------------------------
    // Source: call every frame the source is drawn. The drag starts once the
    // left button, pressed inside `source`, moves 6 px. True while dragging.
    bool BeginDrag(u64 sourceId, const Rect& source, const DragPayload& payload);
    bool IsDragging(std::string_view type) const { return m_drag.IsDragging(type); }
    bool IsDraggingAnything() const { return m_drag.Active(); }
    // The payload being dragged right now (past the start threshold), or null.
    const DragPayload* ActiveDrag() const { return m_drag.Active() ? &m_drag.Payload() : nullptr; }
    // Target: highlights `target` while a `type` payload hovers it; returns the
    // payload on release inside it (once per drop), else nullptr.
    const DragPayload* AcceptDrop(const Rect& target, std::string_view type);

    // Progress bar (determinate). Optional centered text label.
    void ProgressBar(const Rect& rect, f32 fraction, std::string_view text = {});

    // --- Popup menus (see Menu.h) ------------------------------------------
    // Opens a menu built with MenuBuilder. `owner` identifies the opener
    // (IsMenuOpen(owner) lets it render a pressed/open state). OpenMenu
    // places the menu's top-left at `pos` (context menus at the cursor);
    // OpenMenuBelow drops it under `anchor` (buttons). Opening a menu
    // closes any other. The menu is drawn by EndFrame on top of everything
    // and, while open, blocks mouse/keyboard input to every other widget.
    void OpenMenu(u64 owner, Vec2 pos, std::vector<MenuEntry> entries,
                  const MenuOptions& options = {});
    void OpenMenuBelow(u64 owner, const Rect& anchor,
                       std::vector<MenuEntry> entries,
                       const MenuOptions& options = {});
    // Combo box (Starship-style): shows items[current] with a chevron and
    // opens a menu of the items (current one checked) below itself. Returns
    // the index the user picked — on the frame after the pick — or
    // `current` otherwise.
    int Combo(u64 id, const Rect& rect, const std::vector<std::string>& items,
              int current);
    // Toolbar-style button: leading pack icon (tinted `iconColor`)
    // plus a label. `active` draws the pressed/open state (e.g. while the
    // menu it opened is up). Returns true on click.
    bool IconTextButton(u64 id, const Rect& rect, Icon icon,
                        std::string_view label, Color iconColor,
                        bool active = false);
    bool IsMenuOpen(u64 owner) const { return m_menus.IsOpen(owner); }
    bool AnyMenuOpen() const { return m_menus.IsOpen(); }
    void CloseMenus() { m_menus.Close(); }
    const MenuHost& menus() const { return m_menus; }
    // True while an overlay (open menu) owns input this frame: widgets see
    // no hover, clicks, scroll, or keys.
    bool inputBlocked() const { return m_inputBlocked; }

    // --- Docking primitives -------------------------------------------------
    // Splits `region` by `ratio` (0..1) with a draggable divider. Outputs the
    // two sub-regions; updates `ratio` while dragging. Returns true if dragging.
    bool SplitterV(u64 id, const Rect& region, f32& ratio, Rect& left,
                   Rect& right, f32 thickness = 1.0f);
    bool SplitterH(u64 id, const Rect& region, f32& ratio, Rect& top,
                   Rect& bottom, f32 thickness = 1.0f);
    // Draws a docked panel (background, title bar, border); returns the content
    // rect below the title bar.
    Rect PanelWithTitle(const Rect& rect, std::string_view title);

    // A full-width selectable list row (used by the outliner).
    bool Selectable(u64 id, const Rect& rect, std::string_view label,
                    bool selected);
    // Same as above, but draws an optional leading icon (drawn at left).
    bool Selectable(u64 id, const Rect& rect, std::string_view label,
                    bool selected, Icon icon);

    Theme& theme() { return m_theme; }
    Font& font() { return m_font; }
    Font& titleFont() { return m_titleFont; }
    Font& uiFont() { return m_uiFont; }
    Font& mediumFont() { return m_mediumFont; }
    Font& monoFont() { return m_monoFont; }
    // Small muted label font (menu section headers, captions).
    Font& smallFont() { return m_smallFont; }

    // DPI scale the context was initialized with. Token sizes in widgets
    // multiply by this where crispness matters (icons, control heights,
    // focus rings); it doesn't re-bake fonts (they were baked at scale).
    f32 dpiScale() const { return m_dpiScale; }

    // Underlying draw list. Exposed for advanced widgets and icons that need
    // to emit raw geometry between BeginFrame/EndFrame.
    DrawList& drawList() { return m_draw; }
    const DrawList& drawList() const { return m_draw; }
    Vec2 mouse() const { return m_mouse; }
    Vec2 mouseDelta() const { return m_mouseDelta; }
    f32 scrollDelta() const { return m_scroll; }
    // Display size the context was sized with this frame (for widgets that
    // need to clamp floating panels to the screen, e.g. ColorPickerPopup).
    f32 displayWidth() const { return m_displayW; }
    f32 displayHeight() const { return m_displayH; }
    bool isMouseDown(int button) const {
        return button >= 0 && button < 3 ? m_mouseDown[button] : false;
    }

    // Cursor the UI wants this frame (reset to Arrow each BeginFrame); the app
    // applies it via Window::SetCursor after building the UI.
    void RequestCursor(CursorShape shape) { m_requestedCursor = shape; }
    CursorShape RequestedCursor() const { return m_requestedCursor; }
    bool mousePressed(int button) const {
        return button >= 0 && button < 3 ? m_mousePressed[button] : false;
    }
    bool mouseReleased(int button) const {
        return button >= 0 && button < 3 ? m_mouseReleased[button] : false;
    }
    // True on the press of a second click of `button` within kDoubleClickTime
    // (~0.35s) and a few pixels of the previous press. Edge-triggered, cleared
    // every BeginFrame. Used by the Content Browser to activate (open) assets.
    bool mouseDoubleClicked(int button) const {
        return button >= 0 && button < 3 ? m_mouseDoubleClicked[button] : false;
    }
    // True on the frame the Enter key was pressed (edge-triggered, cleared
    // every BeginFrame). Used by text-entry widgets to commit on Enter.
    bool enterPressed() const { return m_keyEnter; }
    // Edge-triggered navigation keys (cleared every BeginFrame). Up/Down move
    // a popup menu's focus row; Left/Right traverse submenus; Home/End jump to
    // the first/last row; Escape dismisses menus / clears search.
    bool keyUp() const { return m_keyUp; }
    bool keyDown() const { return m_keyDown; }
    bool keyLeft() const { return m_keyLeft; }
    bool keyRight() const { return m_keyRight; }
    bool keyEscape() const { return m_keyEscape; }
    bool keyHome() const { return m_keyHome; }
    bool keyEnd() const { return m_keyEnd; }
    // Edge-triggered Tab / Space (focus navigation, checkbox toggle).
    bool keyTab() const { return keyPressed(kGlfwKeyTab); }
    bool keySpace() const { return keyPressed(kGlfwKeySpace); }
    // Edge-triggered Delete key (cleared every BeginFrame). Used by the
    // Material Editor to remove the selected node.
    bool keyDelete() const { return m_keyDelete; }
    // Level-triggered modifier state (tracked from key press/release; not
    // hidden by an open menu so shortcuts can be resolved by the app).
    // Level-triggered: true while the GLFW key is held (e.g. WASD fly keys).
    bool keyHeld(int glfwKey) const {
        return glfwKey >= 0 && glfwKey < kMaxKeys && m_keysHeld.test(static_cast<usize>(glfwKey));
    }
    bool ctrlDown() const { return m_keysHeld.test(341) || m_keysHeld.test(345); }
    bool shiftDown() const { return m_keysHeld.test(340) || m_keysHeld.test(344); }
    bool altDown() const { return m_keysHeld.test(342) || m_keysHeld.test(346); }
    // Clears held-key state (call when the window loses focus so a modifier
    // released elsewhere doesn't stay "stuck").
    void ReleaseAllKeys() { m_keysHeld.reset(); }
    // The real pointer position even while an open menu hides it from
    // widgets (menu bars use it to switch menus on hover, Unreal-style).
    Vec2 rawMouse() const { return m_inputBlocked ? m_stash.mouse : m_mouse; }
    // Edge-triggered press of any GLFW key code (e.g. 291 = F2) this frame.
    bool keyPressed(int glfwKey) const {
        return glfwKey >= 0 && glfwKey < kMaxKeys && m_keysPressed.test(static_cast<usize>(glfwKey));
    }
    // True while a text field (TextField/SearchBox) owns keyboard focus —
    // popup menus use this to leave caret keys (Left/Right/Home/End) to the
    // field instead of hijacking them for navigation.
    bool textFieldFocused() const { return m_focus != 0 && m_focusIsText; }
    // Programmatically gives a text field keyboard focus (e.g. auto-focus a
    // popup's search box when the menu opens) with the caret at `caret`.
    void FocusField(u64 id, usize caret = 0) {
        SetFocus(id, true);
        m_pendingCaret = caret;
    }
    // True while `id` holds keyboard focus (text field or other widget).
    bool IsFieldFocused(u64 id) const { return id != 0 && m_focus == id; }
    // Releases keyboard focus if `id` holds it.
    void BlurField(u64 id) {
        if (id != 0 && m_focus == id) SetFocus(0, false);
    }
    // Seconds since the context started (accumulated frame dt).
    f32 time() const { return m_time; }

    // FNV-1a hash for stable widget ids from string literals.
    static u64 ID(std::string_view s);

private:
    Slate::Image m_brandIcon;
    TextureHandle m_iconAtlas = 0;
    // --- Text-field internals (EditableText, TextField, SearchBox) -------
    // Interaction + editing core (no chrome). `hitRect` is the interactive
    // region (SearchBox passes the whole bar so clicking the icon zone
    // focuses too); `textRect` is where DrawFieldText lays the text out, so
    // clicks map to the same caret positions.
    TextResult EditText(u64 id, const Rect& hitRect, const Rect& textRect,
                        std::string& text, const TextFieldOptions& options);
    // Starts the shared edit state for `id` if it isn't already editing.
    void BeginEdit(u64 id, const std::string& text, const TextFieldOptions& options);
    // Moves keyboard focus; the field losing it gets OnFocusLost.
    void SetFocus(u64 id, bool textEntry);
    // Rounded field chrome: hairline border, field bg, accent focus ring.
    void DrawFieldChrome(const Rect& rect, bool focused);
    // Shared SpinBox / SpinBoxInt implementation over a double.
    NumericResult SpinBoxImpl(u64 id, const Rect& rect, f64& value,
                              const SpinBoxOptions& options, bool integer);
    // Text / placeholder / selection / caret clipped to `rect`; `focused`
    // draws from the shared edit state (scrolled to keep the caret visible).
    void DrawFieldText(const Rect& rect, std::string_view text,
                       std::string_view placeholder, bool focused);

    DrawList m_draw;
    Font m_font;
    Font m_titleFont;
    Font m_uiFont;
    Font m_mediumFont;
    Font m_monoFont;
    Font m_smallFont;
    Theme m_theme = DarkTheme();
    f32 m_dpiScale = 1.0f;

    Vec2 m_mouse;
    Vec2 m_prevMouse;
    Vec2 m_mouseDelta;
    bool m_mouseDown[3] = {};
    bool m_mousePressed[3] = {};
    bool m_mouseReleased[3] = {};
    bool m_mouseDoubleClicked[3] = {};
    f32 m_lastClickTime[3] = {};
    Vec2 m_lastClickPos[3];
    f32 m_scroll = 0.0f;
    static constexpr f32 kDoubleClickTime = 0.35f;
    std::string m_textInput;
    bool m_keyBackspace = false, m_keyDelete = false;
    bool m_keyLeft = false, m_keyRight = false;
    bool m_keyHome = false, m_keyEnd = false, m_keyEnter = false;
    bool m_keyUp = false, m_keyDown = false, m_keyEscape = false;
    static constexpr int kMaxKeys = 512;
    std::bitset<kMaxKeys> m_keysPressed;
    std::bitset<kMaxKeys> m_keysHeld;

    CursorShape m_requestedCursor = CursorShape::Arrow;
    static constexpr int kGlfwKeyTab = 258;
    static constexpr int kGlfwKeySpace = 32;
    ClipboardGet m_clipboardGet;
    ClipboardSet m_clipboardSet;
    std::string m_clipboard;  // used when no handlers are set
    bool m_pointerLock = false;
    std::optional<Vec2> m_pointerRestore;
    u64 m_hot = 0;
    u64 m_active = 0;
    u64 m_focus = 0;
    f32 m_dragGrab = 0.0f;  // thumb grab offset within the scrollbar thumb
    bool m_focusIsText = false;     // m_focus is a text-entry widget
    TextEditState m_edit;           // edit state of the focused text field
    u64 m_editOwner = 0;            // field m_edit belongs to (0 = none)
    u64 m_focusLostId = 0;          // field owed an OnFocusLost commit
    std::optional<usize> m_pendingCaret;  // FocusField caret, applied at BeginEdit
    struct Focusable {
        u64 id;
        bool textEntry;
    };
    std::vector<Focusable> m_focusOrder;  // this frame's focus chain
    u64 m_tabFocusId = 0;
    bool m_focusSeen = false;  // the focused widget was drawn this frame
    // The spin box being pressed / scrubbed (one at a time).
    struct SpinDrag {
        u64 id = 0;
        Vec2 pressPos;
        f32 moved = 0.0f;     // pointer travel since the press, px
        bool dragging = false;
        f64 startValue = 0.0;
        f64 accum = 0.0;      // unsnapped running value
        u32 lastFrame = 0;    // frame the widget was last drawn
    } m_spin;
    // Spin boxes in text mode: the typed text and the text editing began with.
    struct SpinEdit {
        std::string text;
        std::string original;
    };
    std::unordered_map<u64, SpinEdit> m_spinEdits;
    f32 m_time = 0.0f;
    f32 m_displayW = 0.0f;
    f32 m_displayH = 0.0f;

    // Popup / modal / tooltip state — only one of each open at a time so
    // outside-clicks dismiss cleanly.
    u64 m_openPopup = 0;       // id of the open MenuPopup / Dropdown
    u64 m_openModal = 0;       // id of the open BeginModal
    // Tooltip: this frame's hovered candidate, and the one being timed.
    u64 m_tipCandidate = 0;
    std::string m_tipCandidateText;
    u64 m_tipId = 0;
    f32 m_tipStart = 0.0f;
    bool m_tipSuppressed = false;  // a click on the control hides its tip
    void DrawTooltip();
    void DrawDragGhost();
    DragDropState m_drag;
    // Popup menu stack + the input it hides from widgets while open.
    MenuHost m_menus;
    bool m_inputBlocked = false;
    std::unordered_map<u64, int> m_comboPicks;  // Combo id -> picked index
    struct InputStash {
        Vec2 mouse;
        Vec2 mouseDelta;
        bool down[3] = {};
        bool pressed[3] = {};
        bool released[3] = {};
        bool doubleClicked[3] = {};
        f32 scroll = 0.0f;
        std::string text;
        bool backspace = false, del = false, left = false, right = false;
        bool home = false, end = false, enter = false;
        bool up = false, downKey = false, escape = false;
        std::bitset<kMaxKeys> keys;
    } m_stash;
    void BlockInput();
    void RestoreInput();

    std::unordered_map<u64, bool> m_modalOk;
    std::unordered_map<u64, bool> m_modalCancel;

    // Per-id animation values (0..1). Each entry tracks the value plus the
    // last frame it was touched so stale entries can be GC'd.
    struct AnimState { f32 value = 0.0f; u32 lastTouch = 0; };
    std::unordered_map<u64, AnimState> m_anim;
    u32 m_frame = 0;
    static constexpr u32 kAnimGcAge = 120;     // evict after ~2s untouched
    static constexpr f32 kTooltipDelaySec = 0.5f;
};

}  // namespace Luma::Slate
