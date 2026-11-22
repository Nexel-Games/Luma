# Graph Editor + Material Editors — Design (approach A)

Date: 2026-11-22 · Status: approved (approach A) · Branch: `NexaLabs/graph-editor-8c49bb`

## Goal
An Unreal-style visual node-graph Material Editor and a UE-layout Material
Instance Editor, built on a generic, material-agnostic graph editor module.

## Architecture (studied from UE 5.8: SNodePanel/SGraphPanel, UEdGraphSchema,
FMarqueeOperation, FConnectionDrawingPolicy, FMaterialInstanceEditor — no code copied)

```
Editor/GraphEditor/      Luma::GraphEditor   (no material knowledge)
  GraphModel.h           IGraphModel: nodes/pins/links/comments views + edits,
                         CanConnect -> ConnectResponse{Make|Replace|Disallow, msg}
                         (UE schema idea), Actions() for the menu, Snapshot/Restore
                         (std::any) for undo, Copy/Paste (text) for the clipboard.
  ViewTransform.h        pan + discrete zoom levels, graph<->screen, zoom-at-cursor.
  NodeLayout.h           node size + pin anchor positions, hit tests (pure maths).
  Wire.h                 bezier with UE-style clamped tangents, distance-to-curve.
  Selection.h            node/comment selection + marquee ops (Replace/Add/Remove/Invert).
  UndoStack.h            snapshot transactions (Begin/Commit), undo/redo, depth cap.
  GraphController.h      headless operations: link-drag validate+commit, move,
                         delete, copy/paste/duplicate, undo/redo, spawn, reroute, comment.
  GraphEditorWidget.h    Slate drawing + input state machine over the controller.
                         Right-click / drag-off-pin opens a searchable MenuBuilder menu.

Editor/MaterialEditors/  Luma::MaterialEditors
  MaterialGraphModel.h   IGraphModel adapter over Material::GraphMaterial
                         (ExpressionRegistry pins/types, ValueType link rules, comments,
                         "Reroute" expression added to the registry + compiler).
  InstanceParameters.h   headless MI model: parameters of the root material (Translator)
                         grouped/sorted, override flag, effective value, set/reset,
                         parent change (cycle-safe via library).
  PreviewHost.h          IMaterialPreview { Render(path, mesh, yaw, pitch, w, h) } —
                         the hookup adapts Editor/LumaEditor/MaterialPreview.
  MaterialInstanceEditor UE layout: toolbar | preview 70% | Details 30%
                         (General: Parent; Previewing: mesh; Parameter Groups with
                         override checkbox + reset arrow per row).
  MaterialGraphEditor    toolbar Apply/Save | left: preview over Details (material
                         settings or selected node properties) | centre graph |
                         right: searchable palette (drag/click to add) | bottom: Stats/Errors
                         from MaterialSceneBridge::Errors / PendingCompiles (callbacks).
```

Material type rules (mirrors the compiler): Any matches all; Float1 → any float
width; wider → narrower allowed (truncation warning) ; Bool ↔ Bool only;
textures must match exactly. Input pins take one link (Replace response).

## Constraints
New folders only; nothing under Editor/panels, Engine/UI/Slate, EditorScreen,
main.cpp, AssetType.cpp is touched. Hookup into the Content Browser /
EditorScreen is a follow-up once the other UI work lands.

## Testing
Catch2 (`Tests/GraphEditor/*`, `Tests/Editor/MaterialEditors*`): view maths,
layout/hit tests, wire distance, selection/marquee, undo, controller ops on a
fake model, material adapter link rules + copy/paste, instance parameter model.
Widgets are smoke-tested in a standalone screenshot tool if feasible.
