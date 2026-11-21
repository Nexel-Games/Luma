# Luma Engine — Hierarchy Context Menu & Actor Operations

**Document type:** Agent implementation task plan  
**Target:** Existing Luma Editor / World Outliner  
**Owner:** Assigned collaborator (Cursor agent)  
**Status:** Implemented, with limitations in section 6 (no scene undo; parenting does not move actors)  
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

- [x] Locate current context menu code and confirm existing menu behavior.
- [x] Verify whether Rename, Duplicate, Delete, Focus, and Place Actor already work, including shortcuts.
- [x] Locate entity cloning, hierarchy parenting, scene persistence, visibility flags, editor selection, undo/redo commands, and clipboard infrastructure.
- [x] Identify component types and how internal entity references are represented.
- [x] Build the editor and run existing tests before modifications.
- [x] Record relevant files, existing APIs, gaps, and test baseline in a short implementation note.

**Gate:** No new implementation before the baseline is understood.

### Phase 1 — Menu layout and context behavior

- [x] Extend existing popup with ACTOR / HIERARCHY / VIEW / CREATE sections exactly as specified.
- [x] Keep visual styling consistent with Luma's current menu (padding, separators, icon treatment, shortcuts, hover states).
- [x] Anchor popup at mouse location; constrain to screen bounds; close on Escape, outside click, or action selection.
- [x] Handle actor row right-click versus empty-space right-click distinctly.
- [x] Implement disabled states and selection semantics.
- [x] Preserve the Place Actor submenu and current functionality.

**Gate:** Menu renders correctly and existing actions still work.

### Phase 2 — Rename, Duplicate, Delete verification

- [x] Rename via menu and F2, inline within the hierarchy; Enter commits, Escape cancels, focus loss follows existing editor convention.
- [x] Rename changes display name without changing the live entity ID; the name survives scene save/reopen. EnTT ids are recreated on load (pre-existing).
- [x] Duplicate via menu and Ctrl+D; clone supported components and descendant hierarchy, create unique IDs, preserve same parent, select duplicates.
- [x] Keep shared asset references shared. Hierarchy parent links are remapped inside the clone. No other component stores an entity id.
- [x] Use collision-safe readable duplicate names following existing project convention.
- [x] Delete via menu and Delete; remove subtree through existing scene API, clear invalid selections, never delete referenced asset files.
- [ ] Integrate all mutations with existing undo/redo infrastructure. **Blocked:** there is no scene command stack. T-0002 is still a draft. These operations are not undoable. Slate text-field undo is unchanged.

**Gate:** Existing commands pass functional and persistence tests; repair defects only within approved scope.

### Phase 3 — Copy, Cut, Paste, Paste as Child

- [x] Implement an editor-owned actor clipboard using a detached serializable snapshot or existing safe clipboard abstraction; no raw pointers to live entities.
- [x] Copy (`Ctrl+C`) captures selected top-level actor roots, descendants, and supported components. Copy does not modify the scene. Transforms are world-space (there is no local transform).
- [x] Paste (`Ctrl+V`) creates new entities with unique IDs. On actor context, paste as sibling under target's parent; on empty-space context, paste at scene root. The copied world transform is kept.
- [x] Paste as Child places new actor roots under the clicked actor. World position is unchanged, matching Move to Root.
- [x] Reconstruct hierarchy and remap parent links inside the copied set; preserve shared asset path strings.
- [x] Clipboard snapshot remains usable after source actors are deleted or renamed.
- [x] Cut (`Ctrl+X`) stages selected actor roots. The source is destroyed only after paste succeeds. Pasting into a cut actor or its descendant is refused. A missing source is skipped. Cut-paste still creates new entity ids because the clipboard is a snapshot.
- [ ] All clipboard mutations are undoable. **Blocked:** no scene undo stack (T-0002).
- [x] Select newly pasted actors; avoid duplicate processing of nested selections.

**Gate:** Copy/cut/paste pass nested hierarchy, asset-reference, and undo/redo tests.

### Phase 4 — Hierarchy commands

- [x] **Create Empty Child:** create an empty actor under the clicked actor using the existing creation API; give it a unique ID and select it.
- [x] **Select Children:** select descendants; do not select the actor itself or invalid entities. This is the multi-selection. The inspector and gizmo still follow the primary entity.
- [x] **Move to Root:** reparent the clicked actor to the scene root and leave its world-space transform unchanged. Cyclic reparent is rejected. Undo is not available (T-0002).
- [x] Ensure hierarchy expansion and selected rows update immediately after mutations.

**Gate:** Parent/child relationships and transforms survive save/reopen.

### Phase 5 — View commands

- [x] **Focus in Viewport (F):** reuse current camera framing behavior; do not create a new camera controller. Disabled when the actor has no transform.
- [x] **Hide Actor:** editor-only `EditorHiddenComponent` on the actor and its descendants. It is not written to the scene file, so save/reopen shows the actor again. There was no existing visibility flag.
- [x] **Show All Actors:** removes that component from every actor. It does not touch gameplay components.
- [x] Preserve selection and inspector stability when an actor is hidden.
- [ ] Undo/redo visibility changes. **Reported limitation:** no scene command stack, so hide/show is not undoable. No new undo system was added.

**Gate:** Visibility changes affect the intended editor viewport and do not unexpectedly alter runtime behavior.

### Phase 6 — Keyboard shortcuts and focus safety

- [x] F2 Rename; Ctrl+D Duplicate; Ctrl+X Cut; Ctrl+C Copy; Ctrl+V Paste; Delete Delete; F Focus.
- [x] Route keyboard actions through the same command functions used by context-menu entries. The Edit menu calls those functions too.
- [x] Shortcuts follow the existing actor keys: they are editor-wide, and they do not run while a text field or menu has focus. Cut, copy, paste, and Delete are also skipped while the pointer is over the Content Browser.
- [x] Do not intercept normal text copy/paste, delete, or rename while editing names, search boxes, numeric fields, or material properties.
- [x] Respect existing shortcut registry and platform conventions; detect conflicts rather than overriding silently.

**Gate:** All shortcuts work without breaking text editing or other panels.

### Phase 7 — Regression testing and handoff

- [x] Rename keeps the live entity id. Save/reopen keeps the name. EnTT ids are recreated on load; that is pre-existing serializer behavior.
- [x] Duplicate actor with transform, mesh renderer, material reference, and children. Covered by `Tests/Scene/HierarchyTests.cpp`.
- [x] Verify no duplicate IDs and correct internal-reference remapping. The only internal entity reference is the parent link, and it is remapped.
- [x] Copy, delete original, paste; confirm clipboard is independent.
- [x] Copy/paste multiple times; verify all IDs unique.
- [ ] Cut child to another parent; undo/redo; verify transforms and hierarchy. Cut/paste and the world transform are tested. Undo/redo is not available.
- [x] Paste into empty hierarchy space; paste as child; confirm placement semantics. Empty space and Ctrl+V paste at the parent's level (scene root when there is no selection). Paste as Child parents under the clicked actor. World position is unchanged.
- [x] Move to Root preserves world transform. It is not undoable.
- [x] Hide Actor skips meshes, lights, and picking. Show All Actors clears the editor-only flag. Focus reuses the existing camera frame and is disabled without a transform.
- [x] Right-click selected/unselected actor and empty space; verify proper menu variants. Actor menu confirmed in the running editor on Sun. Empty-space menu is Paste (disabled when the clipboard is empty), Show All Actors, and Place Actor.
- [x] Confirm shortcuts are ignored during text input. The existing `textFieldFocused` / open-menu guard returns before the actor keys.
- [x] Test deleted actors, empty clipboard, nested selections, and a paste that would land inside a cut actor. A cut source that no longer exists is skipped.
- [x] Save/reopen scene after reparent and confirm the parent index. Hide flags are intentionally not saved.
- [x] Build Debug and Release configurations available in the repository; run automated tests.
- [x] Perform manual verification in running Luma Editor; do not rely on compilation alone. Opened a scratch copy of MaterialShowcase. Right-click on Sun showed the full menu with Paste, Paste as Child, Select Children, and Move to Root disabled. The scratch editor was closed without saving. Every menu action was not clicked in the viewport; scene behavior is covered by the automated tests.

## 5. Definition of done

- [x] Final menu matches section/action layout above and Luma styling.
- [x] All applicable actions function through both menu and shortcuts.
- [x] Hierarchies, transforms, supported components, and shared asset references are preserved correctly.
- [x] Object IDs are stable on rename and unique for new objects. They are not stable across save/load.
- [x] Scene save/reopen works for names and parent links. Hide is editor-only and is not saved.
- [ ] Undo/redo works for all supported mutations. **Limitation:** no scene undo stack exists (T-0002 is still a draft). None of these operations are undoable. This was not implemented.
- [x] No regressions to existing menu items, viewport selection, or text editing. Development suite: 24181 assertions in 489 test cases, all passed.
- [x] Build and tests pass; manual menu check completed on a scratch project.

## 6. Agent handoff report

### 1. Summary

The World Outliner is a tree. An actor right-click opens Actor / Hierarchy / View / Create: Rename, Duplicate, Cut, Copy, Paste, Paste as Child, Delete, Create Empty Child, Select Children, Move to Root, Focus in Viewport, Hide Actor, Show All Actors, and the existing Place Actor submenu. Empty hierarchy space offers Paste (disabled when the clipboard is empty), Show All Actors, and Place Actor. The Edit menu and the keyboard call the same functions. Right-click keeps an existing multi-selection when the actor is already in it. Select Children is what builds that multi-selection; the inspector and gizmo still follow one primary actor.

Parenting is organizational. `TransformComponent` stays world-space, so Move to Root, reparent, and paste do not move the actor. Hide is an editor-only component on the actor and its descendants. It is skipped by mesh drawing, light gathering, and picking, and it is not written to the scene file.

### 2. Files and APIs

New scene APIs, reused by the editor: `Hierarchy.h` (`SetParent`, `ChildrenOf`, `DescendantsOf`, `DestroySubtree`, `DuplicateSubtree`, `SelectionRoots`), `ActorClipboard`, `Scene::UniqueEntityName`, and optional `"parent"` index in the existing scene serializer. `EditorHiddenComponent` is not reflected and not serialized.

Editor wiring is in `EditorScreen` (one set of operations for the outliner, Edit menu, and shortcuts), `PanelContext`, and `WorldOutliner`. `MaterialSceneBridge::BuildInstances` has a one-line skip of `EditorHiddenComponent` so hidden meshes are not drawn. No second ECS, serializer, or undo stack was added.

Clipboard components: Name, Transform, MeshRenderer, Light, Camera, PostProcess, and the rebuilt parent links. Environment cannot be duplicated, cut, or copied. Asset paths are copied as strings.

### 3. Build and tests

- Development: `LumaTests` and `LumaEditor` linked. `LumaTests.exe` — all tests passed (24181 assertions in 489 test cases). `[scene][hierarchy]` — 51 assertions in 6 cases.
- Debug: `LumaTests` and `LumaEditor` linked. `[scene][hierarchy]` passed (51 assertions in 6 cases).
- Release: `LumaTests` and `LumaEditor` linked. `[scene][hierarchy]` passed (51 assertions in 6 cases).

Expected log noise was left alone: duplicate Vulkan layer warnings, and the two MeshCache errors about an empty mesh upload.

### 4. Manual editor check

Launched Development `LumaEditor` on a temp copy of MaterialShowcase (`%TEMP%\LumaHierarchyScratch`), not the fixture and not TestProject. The scene loaded. Right-click on Sun showed the full menu. Paste, Paste as Child, Select Children, and Move to Root were disabled (empty clipboard, no children, already a root). The editor was closed without saving. Duplicate, cut, paste, hide, and save/reopen were not each clicked in the viewport; those paths are the automated tests above.

### 5. Limitations

- No scene undo/redo. T-0002 is still a draft, and these operations do not go through a command stack. Text-field undo is unchanged.
- Parenting does not compose transforms. World position is what is stored and what is copied.
- Hide does not survive save/reopen.
- Cut-paste assigns new entity ids. The clipboard is a snapshot, and the old actors are destroyed only after paste succeeds. Pasting into a cut actor or one of its descendants is refused, and the sources stay.
- Live entity ids are stable across rename and reparent. Save/load recreates EnTT ids, which was already true. Parent links are stored as indexes into the saved entity array.
- Multi-selection highlights rows and batches the topmost selected actors. The inspector and gizmo still edit the primary selection.
- Actor shortcuts match the existing Ctrl+D / F / Delete behavior: editor-wide, suppressed while a text field or menu is focused, and cut/copy/paste/Delete are also suppressed over the Content Browser.

### 6. What was not modified

Renderer, RHI, shaders, the material editor, the asset import pipeline, physics, and Lua were not modified. The only rendering-adjacent edit is the editor bridge skip for hidden actors.
