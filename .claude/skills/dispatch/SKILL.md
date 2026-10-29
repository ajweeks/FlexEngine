---
name: dispatch
description: Run a multi-agent dispatch on Flex as the manager - split a request list across flex-dev/flex-engineer (and rarely flex-mechanic) subagents working in the shared tree through scripts/flexsvc, watch them, then verify & write the one commit message. Use when asked to dispatch, run agents/a swarm on a list of requests, or act as the agent runner/manager.
---

# Running a dispatch (manager)

You are the manager: you plan, brief, watch & wrap up. The worker agents follow `docs/agents.md` (read it first, so
you know what they already do & don't repeat it in briefs). Don't commit unless asked.

Every subagent costs a startup (docs, claims, the build queue) and a lossy handoff each way: details drop out of the
brief, and their report is a summary you have to check. Split work across agents only where parallel, fresh-context
work pays for that. Everything else you do yourself, because you already hold the whole picture.

## 0. Decide whether to dispatch
- Do it yourself in one session (no agents) when the list is small (about 4 requests or fewer), or when most requests
  touch the same files or build on each other's results. Use the rest of this skill only for the parts that split.
- Otherwise dispatch only the independent clusters (see 2), and keep small or coupled leftovers for yourself.

## 1. Start
- `scripts/flexsvc status`: nothing should be running or holding builds, and the last Release build should pass.
- Archive the previous dispatch: move `board.md`, `claims.json`, `history.jsonl` & `metrics.json` from `$FLEXSVC_HOME`
  (default `C:\Code\Flex\agents`) to `archive/<date>-<name>/`, then delete `cursors/`, `tickets/` & `hold.json`.
- `git status`: note the changes that are already there, so the commit message only covers this dispatch's.

## 2. Plan & brief
- **Split by shared context, not by topic.** For each request, list the files it will likely touch (grep, don't
  guess). Requests that share files, or where one needs another's result, go to one agent, even across areas (UI,
  player, terrain, renderer...). An agent should get work that needs little from the rest of the dispatch. Coupling
  between agents is what broke the shared tree for everyone in past dispatches.
- Run 5–6 agents at most at once (9 hit the usage limit), and fewer if the clusters allow it. Run perf work last, on a
  settled tree, with `run --perf`/`bench`.
- Pick the agent type by task (`.claude/agents/`):
  - `flex-dev` (Sonnet): a scoped feature or bug in one area.
  - `flex-engineer` (Opus): cross-cutting work, e.g. renderer, perf, terrain/streaming, threading, save format, or bugs
    that need investigating.
  - `flex-mechanic` (Haiku): only for a large batch of mechanical edits, e.g. a codebase-wide loc sweep. A few
    mechanical edits (a string into `en.txt`, a Controls row, a config value) cost more as their own agent than the
    edit itself: add them to the brief of the agent already in that area, or do them yourself in wrap-up.
- The `flex-*` types are only offered as subagent types after a session restart that follows adding the files;
  otherwise use a general-purpose agent with a model override and "read `.claude/agents/flex-<type>.md` first".
- The brief needs: the agent's name, the tasks (verbatim from the request list), the files likely involved, what
  "done" looks like & how to verify it (a scene, `FLEX_GAME_TOUR`, a bench). The agent definitions & `docs/agents.md`
  already contain the rules, and the guard hook & `flexsvc done` enforce the important ones.
- Disk is tight: don't give agents git worktrees (a full tree + build copies is GBs). Sandboxes share the repo's
  `resources`/`config` through junctions and only copy exes.

## 3. While they work
- Watch with `scripts/flexsvc status` (builds, runs & waits, holds, claims) & `board.md`.
- **Continue agents instead of re-briefing new ones.** Follow-up work, fixes to what an agent did & board posts meant
  for an agent that has finished go to that same agent with `SendMessage` (it keeps its context). Start a fresh agent
  only when the original's context is clearly bloated, or the work is unrelated. `flexsvc read --undelivered` lists
  directed posts the addressee hasn't read.
- A build that stays broken blocks everyone: find whose claimed files the errors are in & `SendMessage` that agent (or,
  if it's gone, the agent that owns that area now).
- Agents' reports are recaps in the format from `docs/agents.md`; they don't write commit messages.

## 4. Wrap up
- **Check the work, not the recaps.** For each agent, read its actual diff (`git diff -- <its claimed files>`,
  `flexsvc changes`) against its CHANGED/VERIFIED lines, and look at the screenshots or tour images for visual
  changes yourself. A report saying done is not evidence. Send fixes back to the agent (see 3). For a large dispatch,
  you may give one fresh reviewer agent the diff plus the original requests, but not the reports, so its read
  isn't anchored on their conclusions.
- Then `scripts/flexsvc -a manager build` (Windows Release) & `run -- --test-scenes` (add `--frames=1500` for
  streaming work), plus the checks agents marked NOT VERIFIED where you can run them, and the small mechanical
  leftovers you kept for yourself.
- **Record metrics.** Save `scripts/flexsvc metrics` (build count & failures, minutes the Release tree was broken,
  `done` checks per agent) into `$FLEXSVC_HOME/metrics.json`, and add: the requests, the agents per type, wall time,
  tasks done/partial/blocked, and what you had to redo or fix after an agent reported done. Compare against earlier
  dispatches in `archive/` before changing the process, change one thing at a time, and keep what holds up.
- **Finish with one commit message** for everything the dispatch changed. Build it from the diffs & agents' reports,
  in the repo's format: a ≤72 char subject in the present tense, imperative mood and no trailing period (e.g. "Add
  sky visibility to terrain shading"), a blank line, then short bullets or a paragraph covering each change and why.
  Print it in a fenced block at the end of your report.
- Add harness problems the agents hit (lost time, wrong results, missing commands) to `$FLEXSVC_HOME/harness_feedback.md`,
  ordered by impact, with the agents that raised each item in parentheses. Where a rule keeps being broken, enforce it
  in flexsvc or the hook rather than adding more prompt text.
- Report to the user: per agent, what's done, partial or blocked; measured numbers; the metrics vs the last dispatch;
  what nobody could verify (especially visuals); follow-ups; then the commit message.
