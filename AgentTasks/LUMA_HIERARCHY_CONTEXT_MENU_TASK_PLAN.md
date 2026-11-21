# Luma Engine — Hierarchy Context Menu & Actor Operations

**Document type:** Agent implementation task plan  
**Target:** Existing Luma Editor / World Outliner  
**Owner:** Assigned collaborator (Cursor agent)  
**Status:** Ready for implementation  
**Priority:** Medium–High  

## 1. Objective

Extend the **existing** World Outliner right-click context menu into a complete actor-management workflow. The current screenshot already shows **Rename (F2), Duplicate (Ctrl+D), Delete (Delete), Focus in Viewport (F), and Place Actor**. Inspect and preserve those implementations rather than rebuilding them blindly. The screenshot confirms the menu items are visible, **not** that their actions are fully functional; test them.

This task must remain focused on editor UI, scene-object operations, selection, and existing command/serialization APIs. Do not alter rendering, RHI, material/shader compilation, physics, or Lua scripting.

## 2. Required final menu

Use the current Luma dark theme, subdued section labels, separators, and existing menu positioning. The exact layout for an actor right-click is:

```text
ACTOR
  Rename                         F2
  Duplicate                      Ctrl+D
  Cut                            Ctrl+X
  Copy                           Ctrl+C
  Paste                          Ctrl+V
  Paste as Child
  Delete                         Delete
---------------------------------------
HIERARCHY
  Create Empty Child
  Select Children
  Move to Root
---------------------------------------
VIEW
  Focus in Viewport              F
  Hide Actor
  Show All Actors
---------------------------------------
CREATE
  Place Actor                    >
```

Maintain the existing **Place Actor** submenu and its entries. Preserve existing labels unless the new specification explicitly changes them. Do not add unrelated menu features.

### Context-specific variants

- **Right-click actor:** show the full structure above. If the actor is not selected, select it first. If it belongs to an existing multi-selection, preserve that selection for supported batch actions.
- **Right-click empty hierarchy space:** show `Paste` when valid clipboard data exists, `Show All Actors`, and `Place Actor` (including an option to create an empty root actor if already available). Hide actor-specific operations.
- **Unavailable actions:** disable rather than silently fail. Examples: Paste with an empty clipboard; Move to Root on a root actor; Select Children on a leaf; Focus when no suitable bounds/camera API exists.
- **Multi-selection:** support batch operations only where existing scene/selection APIs safely allow them. Deduplicate selected roots so a selected child is not processed twice when its ancestor is also selected. Clearly disable unsupported batch actions.

## 3. Non-negotiable architecture constraints

1. Inspect the repository before editing. Locate hierarchy UI, scene/entity/component model, selection manager, serialization, undo/redo, clipboard, visibility, and shortcut routing.
2. Reuse existing editor commands and scene APIs. Implement a small reusable **Editor Actor Operations** layer only if necessary; UI, keyboard shortcuts, and menu entries should call the same actions.
3. Do not build a second ECS, scene serializer, asset database, or undo framework.
4. Do not alter Vulkan/DirectX/Metal RHI, shaders, material editor, asset import pipeline, physics, or Lua runtime.
5. Preserve persistent actor IDs on rename/move; assign new unique IDs on copy/duplicate. Reuse asset references instead of duplicating mesh/material/texture files.
6. Keep changes on a dedicated feature branch. Avoid unrelated refactors and dependencies.
7. If required APIs are absent, document the dependency and request approval before making invasive changes. Never silently implement a partial feature and call it complete.

## 4. Phase-by-phase implementation

### Phase 0 — Repository reconnaissance and baseline

- [ ] Locate current context menu code and confirm existing menu behavior.
- [ ] Verify whether Rename, Duplicate, Delete, Focus, and Place Actor already work, including shortcuts.
- [ ] Locate entity cloning, hierarchy parenting, scene persistence, visibility flags, editor selection, undo/redo commands, and clipboard infrastructure.
- [ ] Identify component types and how internal entity references are represented.
- [ ] Build the editor and run existing tests before modifications.
- [ ] Record relevant files, existing APIs, gaps, and test baseline in a short implementation note.

**Gate:** No new implementation before the baseline is understood.

### Phase 1 — Menu layout and context behavior

- [ ] Extend existing popup with ACTOR / HIERARCHY / VIEW / CREATE sections exactly as specified.
- [ ] Keep visual styling consistent with Luma's current menu (padding, separators, icon treatment, shortcuts, hover states).
- [ ] Anchor popup at mouse location; constrain to screen bounds; close on Escape, outside click, or action selection.
- [ ] Handle actor row right-click versus empty-space right-click distinctly.
- [ ] Implement disabled states and selection semantics.
- [ ] Preserve the Place Actor submenu and current functionality.

**Gate:** Menu renders correctly and existing actions still work.

### Phase 2 — Rename, Duplicate, Delete verification

- [ ] Rename via menu and F2, inline within the hierarchy; Enter commits, Escape cancels, focus loss follows existing editor convention.
- [ ] Rename changes display name without changing entity ID; survives scene save/reopen.
- [ ] Duplicate via menu and Ctrl+D; clone supported components and descendant hierarchy, create unique IDs, preserve same parent, select duplicates.
- [ ] Keep shared asset references shared. Remap internal entity references that point within the cloned subtree; retain valid external references.
- [ ] Use collision-safe readable duplicate names following existing project convention.
- [ ] Delete via menu and Delete; remove subtree through existing scene API, clear invalid selections, never delete referenced asset files.
- [ ] Integrate all mutations with existing undo/redo infrastructure.

**Gate:** Existing commands pass functional and persistence tests; repair defects only within approved scope.

### Phase 3 — Copy, Cut, Paste, Paste as Child

- [ ] Implement an editor-owned actor clipboard using a detached serializable snapshot or existing safe clipboard abstraction; no raw pointers to live entities.
- [ ] Copy (`Ctrl+C`) captures selected top-level actor roots, descendants, supported components, local transforms, and reference remapping information. Copy does not modify scene.
- [ ] Paste (`Ctrl+V`) creates new entities with unique IDs. On actor context, paste as sibling under target's parent; on empty-space context, paste at scene root. Preserve local transforms for ordinary copy/paste.
- [ ] Paste as Child places new actor roots under clicked actor. Use existing transform/reparent APIs; document whether local or world transform is preserved and make behavior consistent.
- [ ] Reconstruct hierarchy and remap references internal to copied set; preserve valid external shared asset references.
- [ ] Clipboard snapshot remains usable after source actors are deleted or renamed.
- [ ] Cut (`Ctrl+X`) stages selected actor roots for move. Do not destroy source until destination operation succeeds. Define and implement safe behavior if source actor/scene becomes invalid before paste; avoid accidental cross-scene data loss.
- [ ] All clipboard mutations are undoable; redo produces consistent IDs/references according to existing command semantics.
- [ ] Select newly pasted actors; avoid duplicate processing of nested selections.

**Gate:** Copy/cut/paste pass nested hierarchy, asset-reference, and undo/redo tests.

### Phase 4 — Hierarchy commands

- [ ] **Create Empty Child:** create an empty actor under the clicked actor using the existing creation API; give it a unique ID and select it.
- [ ] **Select Children:** select descendants using existing selection API; do not select deleted or invalid entities.
- [ ] **Move to Root:** reparent clicked actor to scene root, preserve world-space transform, reject invalid/cyclic operations, integrate undo/redo.
- [ ] Ensure hierarchy expansion and selected rows update immediately after mutations.

**Gate:** Parent/child relationships and transforms survive save/reopen.

### Phase 5 — View commands

- [ ] **Focus in Viewport (F):** reuse current camera framing behavior; do not create a new camera controller. Handle actors without renderable bounds gracefully.
- [ ] **Hide Actor:** hide selected actor(s) in the editor viewport through the existing visibility mechanism; define whether this is editor-only visibility or serialized scene visibility, and follow established engine semantics.
- [ ] **Show All Actors:** restore visibility for hidden actors in the active scene, without modifying unrelated gameplay/runtime flags unless existing engine behavior explicitly requires it.
- [ ] Preserve selection and inspector stability when an actor is hidden.
- [ ] Undo/redo visibility changes if the existing command architecture supports it; otherwise report the limitation before expanding architecture.

**Gate:** Visibility changes affect the intended editor viewport and do not unexpectedly alter runtime behavior.

### Phase 6 — Keyboard shortcuts and focus safety

- [ ] F2 Rename; Ctrl+D Duplicate; Ctrl+X Cut; Ctrl+C Copy; Ctrl+V Paste; Delete Delete; F Focus.
- [ ] Route keyboard actions through the same command functions used by context-menu entries.
- [ ] Trigger scene shortcuts only when the hierarchy or relevant editor selection context has focus and no text field, popup, or modal is consuming input.
- [ ] Do not intercept normal text copy/paste, delete, or rename while editing names, search boxes, numeric fields, or material properties.
- [ ] Respect existing shortcut registry and platform conventions; detect conflicts rather than overriding silently.

**Gate:** All shortcuts work without breaking text editing or other panels.

### Phase 7 — Regression testing and handoff

- [ ] Rename, save, reopen; verify stable actor ID.
- [ ] Duplicate actor with transform, mesh renderer, material reference, and children.
- [ ] Verify no duplicate IDs and correct internal-reference remapping.
- [ ] Copy, delete original, paste; confirm clipboard is independent.
- [ ] Copy/paste multiple times; verify all IDs unique.
- [ ] Cut child to another parent; undo/redo; verify transforms and hierarchy.
- [ ] Paste into empty hierarchy space; paste as child; confirm placement semantics.
- [ ] Move to Root preserves world transform and is undoable.
- [ ] Hide Actor, Show All Actors, and Focus behave correctly.
- [ ] Right-click selected/unselected actor and empty space; verify proper menu variants.
- [ ] Confirm shortcuts are ignored during text input.
- [ ] Test missing references, deleted actors, empty clipboard, nested selections, and invalid scene transitions.
- [ ] Save/reopen scene after mutations and verify persistence.
- [ ] Build Debug and Release configurations available in the repository; run automated tests.
- [ ] Perform manual verification in running Luma Editor; do not rely on compilation alone.

## 5. Definition of done

- [ ] Final menu matches section/action layout above and Luma styling.
- [ ] All applicable actions function through both menu and shortcuts.
- [ ] Hierarchies, transforms, supported components, and shared asset references are preserved correctly.
- [ ] Object IDs are stable where required and unique for new objects.
- [ ] Scene save/reopen works after rename, duplicate, paste, cut, delete, and reparent.
- [ ] Undo/redo works for all supported mutations, with any limitations explicitly reported.
- [ ] No regressions to existing menu items, viewport selection, or text editing.
- [ ] Build and tests pass; manual verification completed.

## 6. Agent handoff report

At completion, report:

1. Summary of implemented actions and menu behavior.
2. Files changed and architectural APIs reused.
3. Build/test commands executed and actual results.
4. Manual editor test outcomes (including screenshots if useful).
5. Known limitations, unsupported component types, or missing APIs.
6. Confirmation that renderer/RHI/material/physics/Lua code was not modified.

**Agent instruction:** Work in order, validate each phase before proceeding, and do not mark the task finished based only on the visual menu. Functionality, persistence, undo/redo, and regression checks are required.
