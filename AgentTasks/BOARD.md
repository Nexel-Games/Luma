# Task Board

Check this board every time you pull `dev`. Work only on `Approved` or
`Changes Requested` tasks; `Draft` tasks are still being planned by the lead and the
user. Rules: [README.md](README.md).

## Lead is working on (don't touch these areas)

Material graph & compiler (Material Editor project 1 —
`docs/superpowers/specs/2026-11-20-material-graph-compiler-design.md`):
`Engine/Material/**`, `Engine/Rendering/**` (RHI, Vulkan, shaders, new ShaderCompiler),
`Editor/MaterialBridge/**`, `Editor/panels/MaterialEditor/**`,
`Editor/LumaEditor/MaterialPreview.*`, `Tests/Material/**`, `Tests/Rendering/**`.

## New engine services (FYI)

- `Engine/Jobs` — **Luma Job System** (`Luma::Jobs::JobSystem`): worker threads, priorities, dependencies,
  `ParallelFor`, main-thread jobs. The editor owns one; panels get it via `PanelContext::jobs`.
- `Engine/Tasks` — **Task Manager** (`Luma::Tasks::TaskManager`): user-visible progress/cancel/failure;
  `PanelContext::tasks`. Shown by the editor's **Background Tasks** status-bar indicator.
- Don't change your approved plan to use them unless a task says so; ask in *Questions* if useful.

## Tasks

| Id | Title | Status | Area |
|---|---|---|---|
| [T-0000](T-0000-onboarding.md) | Onboarding: learn the project, prove your setup | Approved | report only |
| [T-0001](T-0001-slate-input-followups.md) | Slate input widgets: review follow-ups | Draft (being planned) | `Engine/UI/Slate` |
| [T-0002](T-0002-editor-undo-redo.md) | Editor-wide Undo / Redo (transactions) | Draft (being planned) | Editor, Scene |
| [T-0003](T-0003-asset-import.md) | Asset import pipeline & Import window (models, audio, images) | Completed — review fixes done by the lead; waiting for the user's test | `Engine/Asset`, `Engine/Audio`, import UI |

Start with T-0000 (gate), then T-0003. T-0001 and T-0002 become `Approved` when their plans are final.
