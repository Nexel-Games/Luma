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

Every task is a file `T-XXXX-short-name.md` in this folder with a **Status** line.

| Status | Meaning | Who sets it |
|---|---|---|
| `Proposed` | An idea an agent suggested. **Not approved — don't start.** | engine agent |
| `Approved` | Ready to start. | lead |
| `Plan Review` | You wrote your plan in the task file; waiting for the lead. | engine agent |
| `Plan Approved` | Lead approved the plan; implement it. | lead |
| `In Progress` | You are implementing. | engine agent |
| `Blocked` | You need an answer; the question is in the task's *Questions* section. | engine agent |
| `Completed` | Done, tests green, pushed to `dev`; waiting for the lead to test. | engine agent |
| `Changes Requested` | Lead tested and listed fixes in *Review*. Back to work. | lead |
| `Accepted` | Lead tested and merged toward `main`. Finished. | lead |

**Every time you pull, check this folder** (`git pull origin dev`, then look at
`BOARD.md` and any task whose status changed) **before doing anything else.**

Steps for a task:
1. Pick the lowest-numbered task with status `Approved` (or `Plan Approved` /
   `Changes Requested` assigned to you). Never start `Proposed` tasks.
2. If the task says **Plan required: yes**: write your plan in its *Plan* section
   (files, approach, tests), set `Plan Review`, commit and push to `dev`, then wait
   for `Plan Approved`. You may start another approved task meanwhile.
3. Set `In Progress` (commit + push so the lead sees it), create your branch.
4. Implement test-first. Keep commits small with clear messages.
5. Fill in the task's *Result* section: what changed, files, tests added, how to try it,
   anything left open. Set `Completed`.
6. Rebase on the latest `dev`, run the **whole** test suite, push: merge your branch
   into `dev` and push `dev` (also push your `agent/...` branch).
7. Wait for review. `Changes Requested` → fix, set `Completed` again, push.

Questions: write them in the task's *Questions* section, set `Blocked`, push. Don't
guess on anything that changes what gets built.

New ideas: add a task file with status `Proposed` and a row on the board — the lead
decides.

Commit messages: conventional style (`feat(slate): …`, `fix(editor): …`,
`docs: …`), and mention the task id, e.g. `feat(editor): undo transactions (T-0002)`.
