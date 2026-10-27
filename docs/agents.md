# Working as one of several agents

Several agents edit this tree at the same time. `scripts/flexsvc` (run from WSL) is the only way you should build,
run & talk to the others. Always pass your name: `scripts/flexsvc -a <name> ...`.

## Building & running
- `flexsvc -a NAME build`: builds the tree as it is now (Release). Requests are queued & coalesced, so asking is cheap
  (about 2s when nothing changed). It regenerates the projects if source files were added or removed, fails if the link
  didn't happen, and prints the compile errors. If they're in someone else's files, read the board & wait or tell them;
  don't fix their code.
- `flexsvc -a NAME run [--timeout S] [--env K=V] [--fresh] -- <Flex args>`: builds first & then runs that exact build
  in your own sandbox (it never runs a stale exe). Default timeout 300s. Up to 3 runs share the GPU; it prints the
  error/PASS/FAIL lines, the last lines & the log path. Each sandbox has its own `saved/` (logs, settings, worlds,
  terrain caches, screenshots) but shares `saved/spv` & `saved/fonts`. `--fresh` wipes it first.
- `flexsvc -a NAME run --perf ...`: has the machine to itself (waits for builds & other runs, validation off). Only use
  it for timing.
- `flexsvc -a NAME bench [--seconds 45] [--runs 3] [--baseline r-12]`: the `--play` benchmark (it walks, glides &
  dives in a new world from a fixed seed). Prints p50/p99/max, hitches & top scopes, and the delta against a baseline
  build. Build ids are printed by `build`, and the last 30 are kept. A single run is noisy (p99 can move about 20%), so
  use `--runs 3` before you claim a win.
- `flexsvc -a NAME shaders [name...]`: compiles changed shaders (or the named ones) into `saved/spv`, which Release
  needs. Then run something to check for validation errors.
- `flexsvc -a NAME shots ref SET` / `shots diff SET`: save your last run's screenshots as a reference, or compare
  against one. It prints a mean diff for each image & writes amplified diff images. Look at images only when
  something differs.
- `flexsvc hooks`: every `FLEX_*` env var & command line flag, with its description. Reuse or extend these before
  adding a new one, and give any new one a `// FLEX_NAME=<value>: what it does` comment so it's listed.
- `flexsvc -a NAME scratch`: your private scratch dir (Windows-visible). Never write into another agent's.
- The Linux build (`flexsvc -a NAME build --linux`) is only a compile check. Don't run MSBuild or Flex.exe directly.

## The board
- `build` & `run` print new board messages for you, and `flexsvc -a NAME read` shows them any time.
- `flexsvc -a NAME claim <files...>` when you start: list the files you expect to change. Messages about those
  files reach you.
- `flexsvc -a NAME post "<message>"` as soon as (not at the end):
  - you add a function others might use (give its signature);
  - you change something shared (renderer-wide state, UBO layouts, a common header, save format, input handling);
  - you break the build & are fixing it, or you're blocked.
- `flexsvc -a NAME post --to <agent|file> "<message>"` reaches one agent, or everyone who claimed that file.
  Keep messages to one line.
- The service posts build breaks/fixes & project regenerations itself.

## Final report (≤300 words, this format)
```
STATUS: done | partial | blocked
CHANGED: files, one line each with what changed
SHARED: new APIs / shared behaviour changes (also posted on the board)
VERIFIED: what you ran (command, build id) & the result; numbers if perf
NOT VERIFIED: what you couldn't check (e.g. visuals you didn't look at)
FOLLOW-UP: at most 3 bullets
```

# Dispatching (for the manager)
- Group requests by area (UI/menus, player & traversal, world gen & terrain, renderer, tools/debug, audio), with
  5–6 agents at most at once (9 hit the usage limit). Run perf work last, on a settled tree, with `run --perf`/`bench`.
- Pick the agent type by task (`.claude/agents/`):
  - `flex-mechanic` (Haiku): mechanical edits with an exact recipe, e.g. moving strings into `en.txt`, adding a
    Controls row, renames, adding table entries or a config value.
  - `flex-dev` (Sonnet): a scoped feature or bug in one area.
  - `flex-engineer` (Opus): cross-cutting work, e.g. renderer, perf, terrain/streaming, threading, save format, or bugs
    that need investigating.
- The brief needs: the agent's name, the tasks (verbatim from the request list), the files likely involved, what
  "done" looks like & how to verify it (a scene, `FLEX_GAME_TOUR`, a bench). The agent definitions already contain
  the rules above.
- Watch with `flexsvc status` (builds, runs & waits, claims) & `board.md` in `$FLEXSVC_HOME` (default `C:\Code\Flex\agents`).
  Archive the board between dispatches (move `board.md` aside & delete `cursors/` & `claims.json`).
