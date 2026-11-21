# T-0002 — Editor-wide Undo / Redo (transaction system)

**Status:** Draft
**Assigned to:** engine agent
**Created:** 2026-11-20 by lead
**Area / files:** new module `Editor/Transactions/` (or `Engine/...` if you argue it
belongs there), `Editor/LumaEditor/EditorScreen.*`, `Editor/panels/Inspector/**`,
`Editor/panels/WorldOutliner/**`, `Editor/panels/Viewport/**`,
`Editor/panels/PanelContext.h`, `Engine/Scene/**` (only if needed for snapshots),
`Tests/Editor/**`, `Tests/Scene/**`.
**Do not touch:** `Engine/Material/**`, `Engine/Rendering/**`, `Editor/MaterialBridge/**`,
`Editor/panels/MaterialEditor/**` (lead is working there).

## Goal

Every edit made in the editor can be undone and redone with Ctrl+Z / Ctrl+Y
(and Ctrl+Shift+Z), and from the Edit menu, like Unreal's transaction system
(`GEditor->BeginTransaction` / `FScopedTransaction`). One user gesture = one undo step:
a whole gizmo drag, a whole spin-box scrub, one typed value, one rename, one create /
delete / duplicate. The upcoming node-graph Material Editor will build on this system,
so it must be a reusable, documented module, not Inspector-specific code.

## Requirements

1. **Transaction API** (module with its own folder + public header):
   - `Begin(description)` / `End()` with nesting (only the outermost records a step),
     `Cancel()`, a RAII scoped helper, and `Undo()` / `Redo()` / `CanUndo()` /
     `CanRedo()` / `UndoDescription()` / `RedoDescription()`.
   - Generic recording: objects register "before" state when first modified inside a
     transaction (Unreal's `Modify()` idea). For scene entities, snapshot the
     entity's components with the existing reflection/serialization
     (`Engine/Scene/include/Luma/Scene/ComponentReflection.h`, `SceneSerializer.h`),
     so new components get undo without new undo code.
   - Entity creation / deletion / duplication are undoable (restoring the same entity
     id or remapping references — decide and explain in the plan), including
     hierarchy/parenting if present and the selection.
   - History limit (configurable, default 100 steps) and a memory-conscious design
     (no full-scene snapshots per step).
   - The "dirty" (unsaved changes) state follows undo/redo: undoing back to the saved
     point clears it.
2. **Hook every editor edit path**:
   - Inspector rows: spin boxes use `NumericResult::dragStarted/dragEnded/committed`
     (`Engine/UI/Slate/include/Luma/Slate/Context.h`) so a scrub is one step; typed
     values, checkboxes, combos, colour pickers, material-slot assignment, add/remove
     component.
   - Viewport gizmo translate / rotate / scale drags (one step per drag), drop of a
     material onto an actor.
   - World Outliner rename, create, delete, duplicate, reparent (if supported).
3. **UI**: Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z (not while a text field is focused — the
   field has its own undo), Edit menu entries "Undo <description>" / "Redo
   <description>" greyed when unavailable, `LUMA_USER_INFO` console line on
   undo/redo ("Undo: Move Sun").
4. Selection survives undo/redo sensibly (selected entity deleted by undo → selection
   cleared; re-created by redo → selectable again).

## Acceptance criteria

- [ ] Plan approved before implementation.
- [ ] Unit tests (no GPU): nested transactions record once; undo/redo of component
      edits restores exact values; create/delete/duplicate round trips; history
      limit drops oldest; redo stack cleared by a new edit; cancel discards; dirty
      flag follows the saved point.
- [ ] Integration tests driving the Inspector with the Slate test harness: a spin-box
      scrub across many frames = one undo step; a typed value = one step.
- [ ] Whole suite green; manual check in a scratch MaterialShowcase copy: move,
      rotate, scale, edit light values, rename, duplicate, delete, then undo all the
      way back and redo all the way forward — scene matches at each step.

## Out of scope

- Undo inside the Material Editor (comes with the graph editor; your API must make it
  possible — say how in the plan).
- Multi-user / persistent undo history across sessions.

## References

- UE: `Engine/Source/Editor/UnrealEd/Public/ScopedTransaction.h`,
  `Engine/Source/Runtime/Engine/Classes/Editor/TransBuffer.h`,
  `Engine/Source/Editor/UnrealEd/Private/EditorTransaction.cpp`,
  `UObject::Modify` (`Engine/Source/Runtime/CoreUObject/Public/UObject/Object.h`).
- Luma: `Editor/LumaEditor/EditorScreen.cpp` (keyboard shortcuts ~line 1420, Edit
  menu ~line 1560, `DuplicateEntity` / `DeleteEntity`), `Editor/panels/PanelContext.h`.

## Plan

_(lead + user: being planned — do not start)_

_(engine agent: files/modules, API, snapshot strategy, entity-id strategy, how each
edit path is hooked, how the Material Editor will reuse it, test list)_

## Questions

## Result

## Review
