# Agent Tasks — read this first

This folder is how the **lead agent** hands work to **engine agents** working on Luma
Engine in parallel. If you are an engine agent: read this whole file before doing
anything, then open [BOARD.md](BOARD.md).

---

## 1. What Luma Engine is

Luma Engine is a C++20 game engine with its own editor, built toward **Unreal Engine's
architecture and an AAA look**, in Luma's own style.

- **Renderer:** Vulkan, **deferred only** (G-buffer → lighting → post), PBR materials,
  cascaded sun shadows, sky atmosphere, image-based sky light, bloom, FXAA.
- **UI:** **Luma Slate**, our own immediate-mode UI framework (`Engine/UI/Slate`),
  with Unreal-style widgets (docking, menus, spin boxes, text fields, tooltips…).
- **Editor:** one binary (`LumaEditor.exe`) containing the Project Browser and the
  editor, with Unreal-style panels (Viewport, World Outliner, Inspector, Content
  Browser, Console, Material Editor, Project Settings).
- **Scene:** ECS (EnTT) with components, JSON scene files, reflection/serialization.
- **Assets:** virtual paths `/Game/...` (project `Assets/`) and `/Engine/...`
  (`Content/Engine`); materials `.lmat`, material instances `.lmatinst`.

Big-picture docs (read before your first task):
- `GOAL.md` — master goal and phase tracker (what exists, what is planned).
- `docs/superpowers/specs/` — design docs; `docs/superpowers/plans/` — implementation plans.
- `README.md`, `QUICK_REFERENCE.md`.

## 2. Ground rules (non-negotiable)

1. **Production quality, no shortcuts.** No demos, placeholders, stubbed features,
   hardcoded content or "TODO later" paths. If something can't be done properly, stop
   and ask (see §5).
2. **Unreal is the reference — study, never copy.** The UE 5.8 source may be available
   at `D:/output/UnrealEngine-5.8`. Read it to understand architecture and behaviour;
   never paste or transliterate its code.
3. **Modular architecture.** Every feature is its own module/folder with a clear API.
   The Vulkan backend stays generic: feature modules feed it data through the RHI
   (`Engine/Rendering/RHI`). Group `.cpp` files by subsystem, not into flat folders.
4. **Test-driven.** Write the failing test first (Catch2, `Tests/`), watch it fail,
   implement, watch it pass. The **whole suite must be green** before you push.
5. **Console messages:** user-facing messages use `LUMA_USER_INFO/WARN/ERROR`;
   developer logs use `LUMA_LOG_*` (hidden behind "Show Engine Logs").
6. **Don't touch user projects:** never modify `TestProject/` or anything under the
   user's `LumaProjects` folder. Use scratch copies for manual checks.
7. **Files can mix CRLF/LF line endings.** Edit byte-exact; never rewrite a whole file
   in a way that normalises its line endings.
8. **Never commit secrets**, build output, `Saved/`, or `.superpowers/`.
9. **Stay inside your task's scope** and out of the lead's active areas listed on the
   board ("Lead is working on"). If your task needs a change there, ask first.

## 3. Build, test, run (Windows, MSVC)

```bash
# Close any running LumaEditor.exe first (a locked exe makes the build fail silently).
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" cmd.exe /c "Scripts\\_env.bat cmake --build build --config Development"
./build/bin/Development/LumaTests.exe                 # whole suite
./build/bin/Development/LumaTests.exe "[slate]"       # by tag
./build/bin/Development/LumaEditor.exe --project <copy>.luma --screenshot out.png [--select <Entity>]
```

- First-time configure: see `README.md` / `CMakePresets.json` (Vulkan SDK required).
- Fixture project for manual checks: `Tests/Fixtures/MaterialShowcase` (copy it first).
- Vulkan validation: after a run, `Saved/Logs/Editor.log` must contain no
  `VUID` / `validation error` lines.

## 4. Branches

- `main` — stable. **Engine agents never push to `main`.** The lead merges `dev` into
  `main` after testing.
- `dev` — integration branch. Always start from the latest `dev`.
- Work on your own branch: `agent/T-XXXX-short-name`, created from `dev`.

## 5. Task workflow

**Every task is planned before you get it.** The lead and the user design each task
together (intent, approaches, design, then a step-by-step implementation plan) and
verify it, exactly like the engine's own feature plans in `docs/superpowers/`. The
finished plan is written into the task file. **Your job is to carry out that plan
exactly** — you do not redesign, re-scope or write your own plan.

Every task is a file `T-XXXX-short-name.md` in this folder with a **Status** line.

| Status | Meaning | Who sets it |
|---|---|---|
| `Draft` | Being planned by the lead and the user. **Don't start.** | lead |
| `Proposed` | An idea an agent suggested. **Not approved — don't start.** | engine agent |
| `Approved` | Plan verified and final. Ready to build. | lead |
| `In Progress` | You are implementing. | engine agent |
| `Blocked` | The plan is wrong or unclear somewhere; your question is in *Questions*. | engine agent |
| `Completed` | Done, tests green, pushed to `dev`; waiting for the lead to test. | engine agent |
| `Changes Requested` | Lead tested and listed fixes in *Review*. Back to work. | lead |
| `Accepted` | Lead tested and merged toward `main`. Finished. | lead |

**Every time you pull, check this folder** (`git pull origin dev`, then look at
`BOARD.md` and any task whose status changed) **before doing anything else.**

Steps for a task:
1. Pick the lowest-numbered task with status `Approved` (or `Changes Requested`).
   Never start `Draft` or `Proposed` tasks.
2. Read the whole task file, its *Plan*, and every document it references.
3. Set `In Progress` (commit + push to `dev` so the lead sees it), create your branch
   `agent/T-XXXX-short-name` from `dev`.
4. Execute the plan **step by step, in order**. Each step that writes a test: run it
   and watch it fail before implementing; each `Expected:` line: compare with the
   real output. Commit as the plan says.
5. If a step can't work as written, or contradicts the code you find: **stop**, write
   the problem and your proposed fix in *Questions*, set `Blocked`, push, and wait.
   Small mechanical adjustments (a renamed variable, an include) are fine — list every
   one under *Deviations* in *Result*.
6. Fill in *Result*: what changed, files, tests added, test summary line, how to try
   it, deviations, anything left open. Set `Completed`.
7. Rebase on the latest `dev`, run the **whole** test suite, merge your branch into
   `dev`, push `dev` (and your `agent/...` branch).
8. Wait for review. `Changes Requested` → fix exactly what *Review* lists, set
   `Completed` again, push.

New ideas: add a task file with status `Proposed` and a row on the board. The lead
and the user decide whether it gets planned.

Commit messages: conventional style (`feat(slate): …`, `fix(editor): …`,
`docs: …`), and mention the task id, e.g. `feat(editor): undo transactions (T-0002)`.
