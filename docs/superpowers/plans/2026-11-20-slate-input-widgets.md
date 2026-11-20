# Slate Input Widgets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade Luma Slate's input widgets in place to Unreal-Editor quality (text editing, spin boxes, vector fields, checkbox, Tab focus) and redesign the Inspector material slot on top of them.

**Architecture:** Pure, renderer-free logic (`TextEditState`, `NumericText`) is unit-tested on its own; `Context` widgets drive it. Existing widget signatures stay; `TextField`/`SearchBox`/`DragFloat`/`Vector3Field`/`Checkbox` route through the new cores. Platform features (clipboard, pointer lock) reach Slate through handler hooks so Slate stays platform-free.

**Tech Stack:** C++20, Luma Slate (immediate mode), GLFW (Platform), Catch2.

**Spec:** `docs/superpowers/specs/2026-11-20-slate-input-widgets-design.md`

## Global Constraints

- Study UE 5.8 (`D:/output/UnrealEngine-5.8/Engine/Source/Runtime/Slate/Public/Widgets/Input/`) for behaviour; never copy its code.
- Existing public `Context` widget signatures do not change.
- No hardcoded colours or metrics in widgets: new visual values are `Theme` tokens.
- Slate must not include GLFW or Platform window headers (clipboard / pointer lock go through hooks).
- Every module change keeps its own folder/API (modular architecture).
- Build: kill `LumaEditor.exe` first; `MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" cmd.exe /c "Scripts\\_env.bat cmake --build build --config Development"`.
- Tests: `./build/bin/Development/LumaTests.exe "<tags>"`; the whole suite must stay green.
- Edit files byte-exact (several sources mix CRLF/LF; never rewrite whole files in text mode).
- Don't modify TestProject or LumaProjects; visual checks use scratch copies.

## Review Focus

1. Focus handoff: clicking from one focused text field / spin box straight into another must commit the first (OnFocusLost) and never write the first field's text into the second.
2. A spin box whose bound value is changed by someone else (undo, another panel) while not focused must display the new value; while in text mode it must not be overwritten mid-typing.
3. Pointer lock must always be released — on mouse release, on Escape, and if the dragged widget stops being drawn (panel closed mid-drag).
4. An open popup menu blocks input: a focused field must not receive typed text while a menu is open, and a menu's search box must still work.
5. `textFieldFocused()` must stay true only for text-entry focus (menus rely on it); Tab-focus on a checkbox must not make menus give up arrow keys.

---

### Task 1: `TextEditState` — the text-editing core

**Files:**
- Create: `Engine/UI/Slate/include/Luma/Slate/TextEditState.h`, `Engine/UI/Slate/src/TextEditState.cpp`
- Modify: `Engine/UI/Slate/CMakeLists.txt`, `Tests/CMakeLists.txt`
- Test: `Tests/Slate/TextEditStateTests.cpp`

**Interfaces:**
- Produces:
```cpp
namespace Luma::Slate {
class TextEditState {
public:
    void Begin(std::string_view text, bool selectAll);   // on focus; records Original()
    const std::string& Text() const;
    const std::string& Original() const;
    usize Caret() const; usize Anchor() const;
    bool HasSelection() const;
    std::pair<usize, usize> Selection() const;           // ordered [begin, end)
    std::string SelectedText() const;
    void Insert(std::string_view s);                     // replaces selection, honours maxLength
    void Backspace(bool word); void Delete(bool word);
    void MoveLeft(bool word, bool extend); void MoveRight(bool word, bool extend);
    void Home(bool extend); void End(bool extend);
    void SetCaret(usize pos, bool extend);               // snaps to a codepoint boundary
    void SelectAll(); void SelectWordAt(usize pos);
    std::string Cut();                                   // removes + returns the selection
    bool Undo(); bool Redo();                            // per-field history, 32 steps
    void Revert();                                       // Text() = Original()
    usize maxLength = 0;                                 // 0 = unlimited (bytes)
    f32 scroll = 0.0f;                                   // horizontal scroll, px
};
// Caret index nearest to x (px from text start); measure(prefix) returns its width.
usize CaretFromX(std::string_view text, f32 x, const std::function<f32(std::string_view)>& measure);
}
```
Word rule: a word is a run of alphanumerics/`_`; Ctrl+Left skips spaces then the word/punctuation run; caret moves by UTF-8 codepoint.

- [ ] **Step 1: Write the failing tests** (`[slate][textedit]`):
  - `Insert replaces the selection`: Begin("hello world", false); SetCaret(0,false); SetCaret(5,true); Insert("bye") → Text()=="bye world", Caret()==3, !HasSelection().
  - `Backspace and Delete by word`: Begin("foo bar_baz qux", false); End(false); Backspace(true) → "foo bar_baz "; Home(false); Delete(true) → " bar_baz ".
  - `Ctrl-arrows jump words and Shift extends`: Begin("one two three", false); Home(false); MoveRight(true,false) → Caret()==3; MoveRight(true,true) → Selection()=={3,7}; SelectedText()==" two".
  - `SelectAll, Cut, Undo, Redo`: Begin("abc", false); SelectAll(); Cut()=="abc", Text()==""; Undo() → "abc"; Redo() → "".
  - `Revert restores the focus-start text`: Begin("10", true); Insert("25") → "25"; Revert() → "10".
  - `UTF-8 caret steps by codepoint`: Begin("a\xC3\xA9b", false) (aéb); End(false); MoveLeft(false,false) → Caret()==3; MoveLeft → 1; SetCaret(2,false) → Caret()==1.
  - `maxLength truncates inserts`: maxLength=4; Begin("ab",false); End(false); Insert("cdef") → "abcd".
  - `SelectWordAt picks the run`: Begin("set value_2 now", false); SelectWordAt(6) → SelectedText()=="value_2".
  - `CaretFromX picks the nearest boundary`: measure = 10 px per byte; CaretFromX("abcd", 14, m)==1; (16)==2; (-5)==0; (100)==4.
- [ ] **Step 2:** Build; run `LumaTests.exe "[textedit]"` → Expected: compile failure (header missing).
- [ ] **Step 3:** Implement `TextEditState` / `CaretFromX` (history: push a snapshot {text, caret, anchor} before each mutating op; consecutive single-char inserts coalesce into one step).
- [ ] **Step 4:** Run `"[textedit]"` → all pass; whole suite green.
- [ ] **Step 5:** Commit `feat(slate): TextEditState text-editing core`.

---

### Task 2: `NumericText` — expression evaluator and number formatting

**Files:**
- Create: `Engine/UI/Slate/include/Luma/Slate/NumericText.h`, `Engine/UI/Slate/src/NumericText.cpp`
- Test: `Tests/Slate/NumericTextTests.cpp`

**Interfaces:**
- Produces:
```cpp
namespace Luma::Slate {
std::optional<f64> EvaluateNumericExpression(std::string_view text);  // + - * / ( ), unary -, leading '=' ignored, whitespace ok
std::string FormatNumber(f64 value, int minFractionalDigits, int maxFractionalDigits);
}
```
- [ ] **Step 1: Failing tests** (`[slate][numeric]`):
  - `2*3.5`→7; `=10/4`→2.5; `-(1+2)*-2`→6; ` 1 + 2 * 3 `→7; `1/0`→nullopt; `2+`→nullopt; `abc`→nullopt; `""`→nullopt; `1e3`→1000.
  - FormatNumber(1.0,1,3)=="1.0"; (1.23456,1,3)=="1.235"; (2.5,0,3)=="2.5"; (3.0,0,0)=="3"; (-0.0001,1,3)=="0.0" (no "-0.0").
- [ ] **Step 2:** Run `"[numeric]"` → compile failure.
- [ ] **Step 3:** Implement (recursive-descent parser; `std::from_chars` for literals).
- [ ] **Step 4:** `"[numeric]"` pass; suite green.
- [ ] **Step 5:** Commit `feat(slate): numeric expression evaluator and formatting`.

---

### Task 3: Keyboard edges, clipboard and pointer-lock plumbing

**Files:**
- Modify: `Engine/UI/Slate/include/Luma/Slate/Context.h`, `Engine/UI/Slate/src/Context.cpp`
- Modify: `Engine/Platform/include/Luma/Platform/Window.h`, `Engine/Platform/src/Window/GlfwWindow.{h,cpp}` (and any other `Window` subclass, e.g. test stubs)
- Modify: `Editor/LumaEditor/main.cpp` (wire handlers; apply pointer lock next to `SetCursor`), plus `ProjectBrowser`'s main loop if it has its own
- Test: `Tests/Slate/WidgetTests.cpp`

**Interfaces:**
- Produces (Context):
```cpp
using ClipboardGet = std::function<std::string()>;
using ClipboardSet = std::function<void(const std::string&)>;
void SetClipboardHandlers(ClipboardGet get, ClipboardSet set);
std::string ClipboardText() const;  void SetClipboardText(const std::string&);  // internal store when no handlers
bool keyTab() const;  bool keySpace() const;    // edge-triggered (also stashed/blocked like the others)
void RequestPointerLock(bool locked);            // widgets call each frame while dragging
bool PointerLockRequested() const;               // reset each BeginFrame
std::optional<Vec2> TakePointerRestore();        // where to put the cursor back after a lock ends
```
- Produces (Window): `virtual std::string GetClipboardText() const = 0; virtual void SetClipboardText(const std::string&) = 0; virtual void SetPointerLocked(bool) = 0; virtual void SetCursorPosition(f32 x, f32 y) = 0;` (GLFW: `glfwGet/SetClipboardString`, `GLFW_CURSOR_DISABLED` + raw motion when supported, `glfwSetCursorPos`).
- [ ] **Step 1: Failing tests** (`[slate][widget]`): `Clipboard falls back to an internal store` (SetClipboardText("x") → ClipboardText()=="x"); `Clipboard handlers are used when set` (handlers backed by a local string); `Tab and Space are edge-triggered` (OnKey(258,true) → keyTab() true that frame, false next BeginFrame; Space=32 likewise); `Pointer lock request resets each frame`.
- [ ] **Step 2:** Run → compile failure.
- [ ] **Step 3:** Implement; main.cpp: `ui.SetClipboardHandlers(...)` with the window; after the UI: `window->SetPointerLocked(ui.PointerLockRequested()); if (auto p = ui.TakePointerRestore()) window->SetCursorPosition(p->x, p->y);` (restore before unlocking).
- [ ] **Step 4:** Tests pass; suite green; editor launches (`--screenshot`) with 0 validation errors.
- [ ] **Step 5:** Commit `feat(slate,platform): clipboard, Tab/Space keys and pointer lock`.

---

### Task 4: `EditableText` — full text fields, and Tab focus

**Files:**
- Modify: `Context.h/.cpp` (replace `EditText`/`DrawFieldText` internals), `Theme.h/.cpp` (tokens)
- Test: `Tests/Slate/TextFieldTests.cpp`

**Interfaces:**
- Consumes: Task 1 `TextEditState`, `CaretFromX`; Task 3 clipboard + `keyTab`.
- Produces:
```cpp
enum class TextCommit { None, OnEnter, OnFocusLost, OnCleared };
struct TextFieldOptions {
    std::string_view placeholder;
    bool selectAllOnFocus = false;
    bool readOnly = false;
    usize maxLength = 0;
    bool revertOnEscape = true;   // Escape: Revert() then commit OnCleared
};
struct TextResult { bool changed = false; TextCommit commit = TextCommit::None; };
TextResult EditableText(u64 id, const Rect& rect, std::string& text, const TextFieldOptions& = {});
// Focus chain
void RegisterFocusable(u64 id, bool textEntry);   // called by focusable widgets in draw order
bool FocusedByTab(u64 id) const;                   // true on the frame `id` gained focus via Tab
```
- Theme tokens: `textSelection` (Color), `caretWidth` (f32), `fieldPadX` (f32).
- Behaviour: one `TextEditState` lives in Context for the focused field; the caller's `text` is written from it on each change. Click positions the caret (`CaretFromX` with `m_font.Measure`), drag selects, double-click selects a word, Shift+click extends. Keys: Ctrl+A/C/X/V/Z/Y, Ctrl/Shift arrows, Home/End, Backspace/Delete (Ctrl = word), Enter → OnEnter, Escape → OnCleared (revert), clicking elsewhere or Tab → OnFocusLost. Selection drawn with `textSelection`; `scroll` keeps the caret inside the field. Typing is ignored while `inputBlocked()` except for a menu's own search box (existing behaviour preserved). `textFieldFocused()` is true only for text-entry focus. Tab / Shift+Tab move focus through this frame's `RegisterFocusable` order (wrapping). `TextField` and both `SearchBox` overloads route through `EditableText` and keep returning `bool changed`.
- [ ] **Step 1: Failing tests** (`[slate][textfield]`, NullRenderer + Segoe pattern from WidgetTests): `Typing inserts at caret and Enter commits`; `Escape reverts and reports OnCleared`; `Clicking another field commits the first OnFocusLost and leaves the second's text intact` (Review Focus 1); `Ctrl+A then Ctrl+C copies all text to the clipboard`; `Ctrl+V pastes over the selection`; `Double-click selects a word`; `Tab moves focus to the next registered field, Shift+Tab back, wraps`; `Typed text is ignored while a menu is open` (Review Focus 4); `Existing TextField API still returns changed`.
- [ ] **Step 2:** Run → failures.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** `"[textfield]"`, `"[slate]"` pass; suite green.
- [ ] **Step 5:** Commit `feat(slate): EditableText with selection, clipboard, undo and Tab focus`.

---

### Task 5: `SpinBox` / `SpinBoxInt`; `DragFloat` becomes a wrapper

**Files:**
- Modify: `Context.h/.cpp`, `Theme.h/.cpp`
- Test: `Tests/Slate/SpinBoxTests.cpp`

**Interfaces:**
- Consumes: Task 2 (`EvaluateNumericExpression`, `FormatNumber`), Task 3 (pointer lock), Task 4 (`EditableText` internals, `RegisterFocusable`, `FocusedByTab`).
- Produces: `SpinBoxOptions`, `NumericResult`, `SpinBox`, `SpinBoxInt` exactly as in the spec §4 (`suffix` is `std::string_view`, `accent` is `const Color*`).
- Theme tokens: `spinFill` (slider fill colour), `spinFillHover`, `spinTextModeBg`, `dragThreshold` (f32, 4 px).
- Behaviour (spec §4): press+release under `dragThreshold` → text mode with select-all; drag → scrub by `delta.x * speed` (Shift × `shiftMultiplier`, Ctrl × `ctrlMultiplier`); with slider range and `sliderExponent != 1`, map through the exponent over the range; snap to `delta` when > 0; clamp to `min`/`max`; `RequestPointerLock(true)` while dragging, restore to the press point on release; `dragStarted`/`dragEnded` once each; text-mode commit via evaluator (invalid → revert); display via `FormatNumber` + suffix; fill bar when both slider bounds set; wheel steps when `enableWheel` and hovered. A non-focused spin box always draws the caller's current value (Review Focus 2). If the active spin box isn't drawn in a frame, its drag ends and the lock is released at the next `BeginFrame` (Review Focus 3).
- `DragFloat(id, rect, value, speed, accent)` → `SpinBox(id, rect, value, {.speed = speed, .accent = accent}).changed`.
- [ ] **Step 1: Failing tests** (`[slate][spinbox]`): `Click without drag enters text mode with all selected`; `Typing 2*3 and Enter commits 6`; `Escape in text mode reverts`; `Invalid text reverts on commit`; `Drag of 10 px at speed 0.1 adds 1.0`; `Shift multiplies, Ctrl divides` (10 px → 10.0 / 0.1); `min/max clamp during drag and typed commit`; `delta snaps` (delta 0.25); `dragStarted and dragEnded fire once`; `Pointer lock is requested while dragging and a restore point is given on release`; `Drag ends and lock releases when the widget is not drawn` (Review Focus 3); `External value change shows when not focused` (Review Focus 2); `SpinBoxInt rounds and shows no decimals`; `Tab into a spin box selects all its text`; `DragFloat still scrubs`.
- [ ] **Step 2:** Run → failures.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** `"[spinbox]"`, `"[slate]"` pass; suite green.
- [ ] **Step 5:** Commit `feat(slate): Unreal-style SpinBox (float/int), DragFloat routes through it`.

---

### Task 6: `VectorField`

**Files:** Modify `Context.h/.cpp`; Test `Tests/Slate/SpinBoxTests.cpp`

**Interfaces:**
- Produces: `NumericResult VectorField(u64 id, const Rect& rect, f32* values, int count, const SpinBoxOptions& options = {});` count 2–4; stripes X red, Y green, Z blue (existing gizmo colours), W `theme.textDim`; cell ids `id*8 + i + 1`. `Vector3Field` → `VectorField(id, rect, xyz, 3).changed`. Result fields OR'ed across cells.
- [ ] **Step 1: Failing tests** (`[slate][spinbox]`): `VectorField 4 edits the fourth cell`; `Vector3Field still scrubs its cells`; `Tab walks X→Y→Z`.
- [ ] **Step 2–4:** Run (fail) → implement → pass; suite green.
- [ ] **Step 5:** Commit `feat(slate): VectorField (2-4 components)`.

---

### Task 7: Checkbox — tick glyph, tri-state, focus

**Files:** Modify `Context.h/.cpp`, `Theme.h/.cpp`; Test `Tests/Slate/WidgetTests.cpp`

**Interfaces:**
- Produces: `enum class CheckState { Unchecked, Checked, Undetermined };` and `bool Checkbox(u64 id, const Rect& box, std::string_view label, CheckState& state);` Existing `bool&` overload routes through it. Theme token `checkGlyph` (Color). Drawn tick = two-segment polyline; Undetermined = horizontal dash. Registers as focusable (non-text); Space toggles when focused; focus ring when Tab-focused.
- [ ] **Step 1: Failing tests** (`[slate][widget]`): `Clicking an Undetermined box checks it`; `Space toggles a Tab-focused checkbox`; `Tab focus on a checkbox does not report textFieldFocused` (Review Focus 5); `Checked state draws more vertices than unchecked` (glyph present).
- [ ] **Step 2–4:** fail → implement → pass; suite green.
- [ ] **Step 5:** Commit `feat(slate): checkbox tick glyph, tri-state and keyboard toggle`.

---

### Task 8: Migrate panels to the new widgets

**Files:** `Editor/panels/MaterialEditor/src/MaterialEditor.cpp` (replace `DragClamped` with `SpinBox` + `min/max`; Height Scale/Bump/Normal strength/opacity clip/emissive get slider ranges and fractional digits), `Editor/panels/Inspector/src/Inspector.cpp` (transform: `Vector3Field` unchanged API; light intensity, ranges etc. → `SpinBox` with limits/suffixes where a unit exists, e.g. `°` for rotation), `Editor/panels/ProjectSettings/src/ProjectSettings.cpp` (numeric/text fields), any remaining `TextField` caller that should commit on Enter/focus loss (rename fields in Content Browser / World Outliner use `EditableText` commit instead of their own Enter handling).

- [ ] **Step 1:** For each migrated control with behaviour worth pinning, add/adjust a test in the panel's existing tests if one exists (e.g. Material Editor model edits still apply); otherwise rely on Task 5–7 widget tests and Step 3.
- [ ] **Step 2:** Build; whole suite green.
- [ ] **Step 3:** Visual check: scratch copy of MaterialShowcase; `--select Bumps --screenshot`, Material Editor capture (`--preview-capture` / open asset), Project Settings; 0 `VUID|validation error` in `Saved/Logs/Editor.log`.
- [ ] **Step 4:** Commit `refactor(editor): panels use SpinBox/EditableText`.

---

### Task 9: Inspector material slot redesign

**Files:**
- Modify: `Editor/panels/PanelContext.h` (add `std::function<std::string()> selectedAsset;` — virtual path of the Content Browser selection, empty if none), `Editor/LumaEditor/EditorScreen.cpp` (wire it from `ContentBrowserPanel::Selected()` → virtual path), `Editor/panels/Inspector/src/Inspector.cpp` (`DrawMaterialSlots`), `Editor/panels/Inspector/include/Luma/Editor/Panels/MaterialSlots.h` + `src/MaterialSlots.cpp`
- Test: existing material-slot tests file (grep `AssignMaterialSlot` under `Tests/`)

**Interfaces:**
- Produces: `bool UseSelectedAssetForSlot(MeshRendererComponent& mr, u32 slot, const std::string& selectedPath, const Material::MaterialLibrary& lib);` — assigns only when the path resolves to a Material or Material Instance; returns false otherwise.
- Layout (spec §8): label column "Element N" on the left (Unused N dimmed); thumbnail 48 px with hover border and `AcceptDrop` highlight, double-click → `ctx.onOpenAsset(shown)`; searchable combo (existing `OpenMenuBelow` + `searchable`) showing the name with a chevron; icon buttons with tooltips: Use Selected Asset (arrow-left icon), Browse to Asset (search icon), Reset to Default (reset icon); Create Instance moves into the combo's menu as an entry under a "Actions" section. Empty slot shows "DefaultMaterial" in `textDim`. Icons from the existing icon pack (`Content/Editor/Icons`).
- [ ] **Step 1: Failing tests**: `UseSelectedAssetForSlot assigns a material`; `...assigns an instance`; `...rejects a texture path and leaves the slot unchanged`.
- [ ] **Step 2–4:** fail → implement → pass; suite green.
- [ ] **Step 5:** Visual check of the Inspector with `--select Bumps`; 0 validation errors.
- [ ] **Step 6:** Commit `feat(editor): Unreal-style material slot`.

---

### Task 10: Docs

**Files:** `GOAL.md` (Slate section: list the new widgets and behaviours).
- [ ] **Step 1:** Update docs; suite green.
- [ ] **Step 2:** Commit `docs: Slate input widgets`.
