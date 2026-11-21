# T-0001 — Slate input widgets: review follow-ups

**Status:** Draft
**Assigned to:** engine agent
**Created:** 2026-11-20 by lead
**Area / files:** `Engine/UI/Slate/**`, `Tests/Slate/**`

## Goal

Close the six minor issues the final code review found in the new Slate input widgets
(text-edit core, `EditableText`, `SpinBox`, Tab focus). Each is small, but together
they make the widgets robust enough for the node-graph Material Editor, which will put
dozens of these fields inside nodes.

Background: `docs/superpowers/specs/2026-11-20-slate-input-widgets-design.md` and the
plan `docs/superpowers/plans/2026-11-20-slate-input-widgets.md`. The core lives in
`Engine/UI/Slate/src/Context.cpp` (`EditText`, `SetFocus`, `SpinBoxImpl`,
`EndFrame` Tab handling), `TextEditState.{h,cpp}`, `NumericText.{h,cpp}`.
Shared test harness: `Tests/Slate/SlateTestHarness.h`.

## Requirements (one failing test first for each)

1. **M1 — focus-lost is lost when focus moves twice.** `m_focusLostId` holds one id.
   If focus moves A→B→C before A is drawn again, A never receives `OnFocusLost`, and
   a spin box in text mode drops the typed value. Make the pending focus-lost
   notifications a small set (or commit eagerly), so every field that lost focus gets
   exactly one `OnFocusLost`. Test: three spin boxes; A typed into; focus moved to B
   then C via `FocusField` in the same frame before A draws → A commits its typed
   value exactly once.
2. **M2 — menu search box gets a spurious OnFocusLost.** `EndFrame` calls
   `BlurField(MenuHost::kSearchFieldId)` when no menu is open, which queues a
   focus-lost for the search field, delivered on the next menu's first frame. Internal
   blurs must not queue a commit. Test: open a searchable menu, close it, open another
   → its search box sees no `OnFocusLost` (expose via `EditableText` result in a test
   field that mimics it, or test through `SetFocus`/`BlurField` semantics).
3. **M4 — integer / float overflow.** `SpinBoxInt` casts `llround(v)` to `i32` with no
   range check, and `SpinBox` casts any `f64` to `f32`. Clamp to the destination
   type's representable range before the cast (in addition to `min`/`max`). Tests:
   typing `1e12` into an unbounded `SpinBoxInt` gives `INT32_MAX`; `-1e12` gives
   `INT32_MIN`; typing `1e300` into `SpinBox` gives `FLT_MAX`.
4. **M5 — `maxLength` truncation resets the edit on the focusing frame.**
   `TextEditState::Begin` truncates to `maxLength`; `EditText` then sees
   `text != m_edit.Text()` and re-`Begin`s, wiping select-all / the clicked caret, and
   writes the truncated text back to the caller without a change report. Fix so the
   caller's text is only truncated when the user edits, and the click/select-all state
   survives. Test: `maxLength = 4`, caller text `"abcdef"`, click with
   `selectAllOnFocus` → selection covers the whole (truncated) text and typing `"x"`
   yields `"x"`; the caller's string is unchanged until a keystroke.
5. **M6 — `CaretFromX` is O(n²).** It measures every prefix. Make it O(n) by
   accumulating per-codepoint advances (add a `Font` helper or pass an advance
   callback). Keep results identical (existing tests must stay green); add a test with a
   2 000-character string asserting the same answer as a reference implementation.
6. **M7 — stale spin-box text flashes for one frame.** When a spin box is drawn again
   after focus moved away without a commit, it shows its old typed text for one frame
   before the entry is erased. Erase stale `m_spinEdits` entries before drawing (e.g.
   when the id is neither focused nor the edit owner, draw the bound value). Test:
   draw-call capture or result inspection showing the bound value on that frame.

## Acceptance criteria

- [ ] One new test per item above, each seen failing before the fix.
- [ ] `./build/bin/Development/LumaTests.exe` all green.
- [ ] Manual: in a scratch copy of MaterialShowcase, Tab through the Inspector Light
      fields, type values, click between fields and other actors — values land where
      typed, nothing is lost.

## Out of scope

- New widgets, visual restyling, anything outside `Engine/UI/Slate` and its tests.

## References

- UE: `Engine/Source/Runtime/Slate/Public/Widgets/Input/SSpinBox.h`,
  `SEditableText.h` (commit/focus behaviour).

## Plan

_(lead + user: being planned — do not start)_

_(not required)_

## Questions

## Result

## Review
