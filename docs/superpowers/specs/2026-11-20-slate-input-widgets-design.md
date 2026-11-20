# Slate Input Widgets — Design

**Date:** 2026-11-20
**Status:** awaiting approval

## Intent

Bring Luma Slate's input widgets up to Unreal Editor quality, studied from
UE 5.8's `SSpinBox`, `SNumericEntryBox`, `SEditableText`, `SCheckBox` and
`SVectorInputBox` (architecture and behaviour only — no UE code is copied).

**Why now:** the next project is the node-graph Material Editor. Its nodes
carry inline value widgets (pin defaults, vectors, colors, renamable titles,
comments). Those must be solid before the graph is built. Every existing
panel (Inspector, Material Editor, Content Browser, Project Settings, World
Outliner, Project Browser, Color Picker) benefits on the way.

## Goals

- One text-editing core with the behaviour users expect from any editor field.
- An Unreal-grade numeric field (spin box) for floats and ints.
- Vector 2/3/4 fields, a proper checkbox, Tab keyboard navigation.
- The Inspector material slot redesigned on top of the new widgets.

## Non-goals

- A retained-mode widget tree (Slate stays immediate-mode).
- An editor-wide undo system. The widgets *emit* the begin/end events it
  will hook into; building it is separate work.
- Zoom-scaled widgets for the node graph canvas (the graph project owns that).
- Non-ASCII typing (the font atlas only bakes ASCII today). The edit core is
  UTF-8 safe so this can be turned on later.

## Anti-goals

- Changing the signatures existing panels call. Upgrades happen *in place*:
  `TextField`, `SearchBox`, `DragFloat`, `Vector3Field`, `Checkbox` keep
  their signatures and gain the new behaviour.
- Copying UE source.

## Approaches (decided)

1. **Upgrade in place + a few new widgets** — *chosen.* One widget system;
   every panel improves at once; the graph reuses the same calls.
2. Separate retained widget classes modelled on SSpinBox — rejected: two
   systems side by side and a migration later.
3. Only spin box + text editing now — rejected by the user in favour of 1.

## Design

### 1. Text-edit core — `TextEditState` (new, `Engine/UI/Slate`)

Pure logic, no drawing, unit-testable. Holds text, caret, selection anchor,
horizontal scroll, the value at focus start (for revert) and a small per-field
undo history.

Operations: insert (replaces the selection), Backspace / Delete (Ctrl = by
word), Left / Right (Ctrl = by word, Shift = extend selection), Home / End,
Ctrl+A, Ctrl+C / X / V, Ctrl+Z / Y, click to place the caret, drag to
select, double-click to select a word, Escape reverts to the focus-start text.
The caret is kept visible by scrolling long text horizontally. Caret
positions are UTF-8 codepoint boundaries.

Commit model (like UE's `ETextCommit`): `enum class TextCommit { None,
OnEnter, OnFocusLost, OnCleared }`. New call returning the full result:

```cpp
struct TextResult { bool changed; TextCommit commit; };
TextResult EditableText(u64 id, const Rect&, std::string& text, const TextFieldOptions&);
```

`TextField` and `SearchBox` keep returning `bool` and route through it.
Options: placeholder, select-all on focus, read-only, max length.

### 2. Clipboard

`Window` gains `GetClipboardText()` / `SetClipboardText()` (GLFW).
Slate stays platform-free: `Context::SetClipboardHandlers(get, set)`; the app
wires them to the window. With no handlers, an internal clipboard is used
(tests).

### 3. Keyboard

`OnKey` additionally tracks A, C, V, X, Z, Y and Tab edges. **Tab focus:**
focusable widgets register in draw order each frame; Tab / Shift+Tab move
focus to the next / previous one. A spin box receiving focus by Tab enters
text mode with all text selected (UE behaviour); a checkbox toggles on Space.

### 4. Numeric field — `SpinBox` (new)

```cpp
struct SpinBoxOptions {
    std::optional<f64> min, max;              // hard limits (clamp)
    std::optional<f64> sliderMin, sliderMax;  // range shown as a fill bar
    f64 delta = 0;            // snap step (0 = continuous)
    f64 speed = 0.02;         // value per pixel dragged
    f32 shiftMultiplier = 10, ctrlMultiplier = 0.1f;
    f32 sliderExponent = 1;   // non-linear drag over the slider range
    int minFractionalDigits = 1, maxFractionalDigits = 3;
    bool enableWheel = false;
    std::string_view suffix;  // unit text, e.g. "cm", "°"
    const Color* accent = nullptr;  // axis stripe (X/Y/Z)
};
struct NumericResult { bool changed, committed, dragStarted, dragEnded; };
NumericResult SpinBox(u64 id, const Rect&, f32& value, const SpinBoxOptions& = {});
NumericResult SpinBoxInt(u64 id, const Rect&, i32& value, const SpinBoxOptions& = {});
```

Behaviour:
- Press and release without moving past the drag threshold (4 px) → text
  mode, all text selected.
- Drag → scrub. Shift ×10, Ctrl ×0.1. The pointer is hidden and locked
  during the drag (infinite drag, like UE), then shown again where the drag
  began. Platform: `Context::RequestPointerLock` → `Window::SetPointerLocked`.
- With a slider range, a fill bar shows where the value sits in the range;
  dragging maps through `sliderExponent`.
- Text mode: Enter or focus loss commits, Escape reverts. The input is
  parsed by a small math-expression evaluator (`+ - * / ( )`, unary minus,
  decimals), so `2*3.5` and `=10/4` work as in UE. Invalid input reverts.
- Display: trailing zeros trimmed between min and max fractional digits;
  the integer variant shows no decimals.
- Mouse wheel steps by `delta` (or speed) when `enableWheel`.

`DragFloat(id, rect, value, speed, accent)` becomes a wrapper over `SpinBox`.
Panels' local clamp helpers (Material Editor `DragClamped`) are replaced by
`min`/`max`.

### 5. Vector fields — `VectorField` (new)

`NumericResult VectorField(u64 id, const Rect&, f32* v, int count, const SpinBoxOptions&)`
for 2/3/4 components; X/Y/Z/W stripes red/green/blue/grey. `Vector3Field`
wraps it.

### 6. Checkbox

A drawn tick mark (not a filled square), hover and pressed states, a focus
ring when it has Tab focus. New tri-state overload
`bool Checkbox(u64, const Rect&, std::string_view label, CheckState& state)`
with `CheckState { Unchecked, Checked, Undetermined }` (a dash) for
multi-selection; clicking an Undetermined box checks it.

### 7. Theme

New tokens in `Theme`: spin box fill-bar color, text-mode background,
selection highlight, check-glyph color, focus-ring width. No hardcoded
colours in widgets.

### 8. Inspector material slot (Editor)

```
Element 0   [thumb]  [ M_Bricks                ▾ ]  (→) (🔍) (↺)
```

- Name is a searchable combo listing the project's materials and instances.
- Buttons with icons and tooltips: Use Selected Asset (from the Content
  Browser), Browse to Asset, Reset to Default.
- Thumbnail: hover highlight, drop highlight, double-click opens the
  Material Editor.
- An empty slot shows the Default Material dimmed.

## Testing

- Unit tests: `TextEditState` (every operation above), the expression
  evaluator, number formatting.
- Widget tests driving `Context` with the NullRenderer pattern in
  `Tests/Slate/WidgetTests.cpp`: click-without-drag enters text mode; drag
  with Shift/Ctrl scales; min/max clamp; Enter commits, Escape reverts;
  Tab order; tri-state checkbox; clipboard round trip.
- Visual check: editor screenshots of the Inspector, Material Editor and
  Project Settings, 0 validation errors.

## Left to the builder

Exact pixel metrics within the theme tokens; the internal undo depth per
field (small, e.g. 32 steps); the order of migrating call sites.

## Calls I made (for review)

- Pointer lock (cursor hidden) during spin-box drags, like UE.
- Math expressions in numeric fields.
- Non-ASCII typing deferred; the core is UTF-8 safe.
- The field-level undo (Ctrl+Z inside a text field) is separate from the
  future editor-wide undo.
