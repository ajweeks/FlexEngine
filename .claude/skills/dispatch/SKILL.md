---
name: dispatch
description: Run a multi-agent dispatch on Flex as the manager - split a request list across flex-mechanic/flex-dev/flex-engineer subagents working in the shared tree through scripts/flexsvc, watch them, then verify & write the one commit message. Use when asked to dispatch, run agents/a swarm on a list of requests, or act as the agent runner/manager.
---

# Running a dispatch (manager)

You are the manager: you plan, brief, watch & wrap up. The worker agents follow `docs/agents.md` (read it first, so
you know what they already do & don't repeat it in briefs). Don't commit unless asked.

## 1. Start
- `scripts/flexsvc status`: nothing should be running or holding builds, and the last Release build should pass.
- Archive the previous dispatch: move `board.md` & `claims.json` from `$FLEXSVC_HOME` (default `C:\Code\Flex\agents`)
  to `archive/<date>-<name>/`, then delete `cursors/`, `tickets/` & `hold.json`.
- `git status`: note the changes that are already there, so the commit message only covers this dispatch's.

## 2. Plan & brief
- Group requests by area (UI/menus, player & traversal, world gen & terrain, renderer, tools/debug, audio), with
  5–6 agents at most at once (9 hit the usage limit). Run perf work last, on a settled tree, with `run --perf`/`bench`.
- Pick the agent type by task (`.claude/agents/`):
  - `flex-mechanic` (Haiku): mechanical edits with an exact recipe, e.g. moving strings into `en.txt`, adding a
    Controls row, renames, adding table entries or a config value.
  - `flex-dev` (Sonnet): a scoped feature or bug in one area.
  - `flex-engineer` (Opus): cross-cutting work, e.g. renderer, perf, terrain/streaming, threading, save format, or bugs
    that need investigating.
- The `flex-*` types are only offered as subagent types after a session restart that follows adding the files;
  otherwise use a general-purpose agent with a model override and "read `.claude/agents/flex-<type>.md` first".
- The brief needs: the agent's name, the tasks (verbatim from the request list), the files likely involved, what
  "done" looks like & how to verify it (a scene, `FLEX_GAME_TOUR`, a bench). The agent definitions already contain
  the rules.
- Disk is tight: don't give agents git worktrees (a full tree + build copies is GBs). Sandboxes share the repo's
  `resources`/`config` through junctions and only copy exes.

## 3. While they work
- Watch with `scripts/flexsvc status` (builds, runs & waits, holds, claims) & `board.md`.
- Directed board posts to an agent that has finished are lost: check `flexsvc read --undelivered` (posts to an agent
  or a claimed file that the addressee hasn't read) and relay them in that agent's next brief or to whoever owns the
  work now.
- A build that stays broken blocks everyone: find whose claimed files the errors are in & tell them (or, if they've
  finished, give the fix to an agent that owns that area).
- Agents' reports are recaps in the format from `docs/agents.md`; they don't write commit messages.

## 4. Wrap up
- Once every agent has reported: `scripts/flexsvc -a manager build` (Windows Release) & `run -- --test-scenes`
  (add `--frames=1500` for streaming work), plus the checks from the briefs that agents marked NOT VERIFIED where you
  can run them.
- **Finish with one commit message** for everything the dispatch changed. Build it from the agents' reports &
  `flexsvc changes`, in the repo's format: a ≤72 char subject in the present tense, imperative mood and no trailing
  period (e.g. "Add sky visibility to terrain shading"), a blank line, then short bullets or a paragraph covering each
  change and why. Print it in a fenced block at the end of your report.
- Add harness problems the agents hit (lost time, wrong results, missing commands) to `$FLEXSVC_HOME/harness_feedback.md`,
  ordered by impact, with the agents that raised each item in parentheses.
- Report to the user: per agent, what's done, partial or blocked; measured numbers; what nobody could verify
  (especially visuals); follow-ups; then the commit message.
