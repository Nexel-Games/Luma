# T-0000 — Onboarding: learn the project and prove your setup

**Status:** Approved
**Assigned to:** engine agent
**Plan required:** no
**Created:** 2026-11-20 by lead
**Area / files:** `AgentTasks/T-0000-onboarding.md` only (no code changes)

## Goal

You understand what Luma Engine is, how it is organised and how we work, and your
machine builds, tests and runs it. This is the gate for every other task.

## Requirements

1. Read `AgentTasks/README.md` completely, then `GOAL.md` (skim the phases, read the
   ground rules and Phase 4 Slate + Phase 5 Editor sections),
   `docs/superpowers/specs/2026-11-20-slate-input-widgets-design.md` and
   `docs/superpowers/specs/2026-11-20-material-graph-compiler-design.md`.
2. Build everything (Development) and run the whole test suite.
3. Copy `Tests/Fixtures/MaterialShowcase` to a scratch folder and capture a screenshot:
   `LumaEditor.exe --project <copy>/MaterialShowcase.luma --select Bumps --screenshot onboarding.png`.
   Check `Saved/Logs/Editor.log` for validation errors.
4. Fill in *Result* below:
   - test summary line (e.g. `All tests passed (N assertions in M test cases)`),
   - validation error count,
   - anything that failed or was confusing in the setup (exact errors),
   - a short description, in your own words, of: the Slate immediate-mode model,
     how an editor panel gets its data (`Editor/panels/PanelContext.h`), and how a
     material reaches the GPU today (Material → bridge → RHI → Vulkan material table).
5. Set status `Completed`, commit on `dev`
   (`docs(agent): onboarding report (T-0000)`) and push.

Do not attach the screenshot to the repo; describe what you saw.

## Acceptance criteria

- [ ] Result section filled in with the items above.
- [ ] No code or asset changes in the commit.

## Out of scope

- Fixing anything you find. Report it in *Result*; the lead turns it into tasks.

## Plan

_(not required)_

## Questions

## Result

## Review
