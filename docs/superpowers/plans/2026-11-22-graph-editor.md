# Graph Editor + Material Editors — Plan

Spec: `docs/superpowers/specs/2026-11-22-graph-editor-design.md`
Build: `cmd /c "Scripts\_env.bat cmake --build build --config Development"`
Test: `./build/bin/Development/LumaTests.exe` (base: 587 cases green)

Global constraints: new folders only (see spec); `LUMA_LOG_*` engine / `LUMA_USER_*`
user; popups via MenuBuilder + Context::OpenMenu, lambdas capture by value.
Commit per task, trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

Execution order: Task 5 first (instance editor first, per approach A), then 1–4, 6, 7.

## Task 1 — GraphEditor core (headless)
Files: `Editor/GraphEditor/{CMakeLists.txt,include/Luma/GraphEditor/*.h,src/*.cpp}`,
root CMake `add_subdirectory`, `Tests/GraphEditor/GraphEditorCoreTests.cpp`.
TDD: ViewTransform round-trip + zoom-at-cursor keeps the point fixed; zoom
clamps to levels; NodeLayout pin anchors/hit test; Wire distance; Selection
marquee ops; UndoStack begin/commit/undo/redo/cap. Commit `feat(grapheditor): core maths, selection, undo`.

## Task 2 — GraphController + IGraphModel
`GraphModel.h`, `GraphController.{h,cpp}`; tests with a fake model: link drag
from output or input, Disallow keeps graph, Replace swaps, delete selection
(skips non-removable), copy/paste offsets + selects pasted, undo/redo of each op,
move commits one transaction, reroute insert on a wire. Commit `feat(grapheditor): controller and model interface`.

## Task 3 — GraphEditorWidget (Slate)
Grid, comments, nodes, pins, wires, link drag preview + response tooltip,
marquee, pan (RMB/MMB drag), wheel zoom, keyboard (Del, Ctrl+C/V/D/Z/Y, C =
comment), searchable context menu. Build-only + smoke. Commit `feat(grapheditor): Slate graph widget`.

## Task 4 — Reroute expression + MaterialGraphModel
Registry "Reroute" (Utility, Any→Any, hidden in palette) + compiler pass-through;
adapter with type rules; tests (compile through a reroute; link rules; copy/paste
round trip; snapshot restore). Commit `feat(material-editors): material graph adapter`.

## Task 5 — InstanceParameters + Material Instance Editor
Headless model tests (groups, override/reset, inherited value through a chain,
parent change refused on cycle). Panel UI. Commit `feat(material-editors): material instance editor`.

## Task 6 — Material Graph Editor
Panel UI (layout per spec); palette filter logic tested (`FilterPalette`).
Commit `feat(material-editors): node graph material editor`.

## Task 7 — Smoke screenshot tool (if feasible) + follow-up notes
`Tools/MaterialEditorSmoke` renders both editors offscreen to PNG. Document hookup.
