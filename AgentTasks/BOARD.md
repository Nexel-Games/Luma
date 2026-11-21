# Task Board

Check this board every time you pull `dev`. Work only on `Approved`,
`Plan Approved` or `Changes Requested` tasks. Rules: [README.md](README.md).

## Lead is working on (don't touch these areas)

Material graph & compiler (Material Editor project 1 —
`docs/superpowers/specs/2026-11-20-material-graph-compiler-design.md`):
`Engine/Material/**`, `Engine/Rendering/**` (RHI, Vulkan, shaders, new ShaderCompiler),
`Editor/MaterialBridge/**`, `Editor/panels/MaterialEditor/**`,
`Editor/LumaEditor/MaterialPreview.*`, `Tests/Material/**`, `Tests/Rendering/**`.

## Tasks

| Id | Title | Status | Plan required | Area |
|---|---|---|---|---|
| [T-0000](T-0000-onboarding.md) | Onboarding: learn the project, prove your setup | Approved | no | report only |
| [T-0001](T-0001-slate-input-followups.md) | Slate input widgets: review follow-ups | Approved | no | `Engine/UI/Slate` |
| [T-0002](T-0002-editor-undo-redo.md) | Editor-wide Undo / Redo (transactions) | Approved | **yes** | Editor, Scene |

Order: T-0000 first (gate). Then T-0001; write the T-0002 plan while T-0001 is in
review.
