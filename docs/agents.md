# Working as one of several agents

Several agents edit this tree at the same time. `scripts/flexsvc` (run from WSL) is the only way you should build,
run & talk to the others. Always pass your name: `scripts/flexsvc -a <name> ...`.

## Rules
- Build, run, benchmark & compile shaders only through `flexsvc -a NAME ...`. A hook blocks subagents from running
  MSBuild, ninja or Flex.exe directly. Never use another agent's sandbox or scratch dir.
- Post new shared APIs & shared behaviour changes to the board (`flexsvc post`) when you make them.
- Never revert or "fix" changes you did not make. If the build is broken in someone else's files, check the board,
  post `--to` them & wait (or work against `run --last-good`).
- Do not commit, and don't write a commit message (the manager writes one for the whole dispatch). Work in the main
  tree.
- Before your final report, `flexsvc -a NAME done` must pass: the newest Release build passes & includes your claimed
  files, no `DBG:` prints are left in them, and any hold you forgot is released.

## Building & running
- `flexsvc -a NAME build`: builds the tree as it is now (Release). Requests are queued & coalesced, so asking is cheap
  (about 2s when nothing changed). It regenerates the projects if source files were added or removed, fails if the link
  didn't happen, and prints the compile errors. If they're in someone else's files, read the board & wait or tell them;
  don't fix their code.
- `flexsvc -a NAME run [--timeout S] [--env K=V] [--fresh] -- <Flex args>`: builds first & then runs that exact build
  in your own sandbox (it never runs a stale exe). Default timeout 300s. Up to 3 runs share the GPU; it prints the
  error/PASS/FAIL lines, the last lines & the log path. Each sandbox has its own `saved/` (logs, settings, worlds,
  terrain caches, screenshots); its `saved/spv` is the build's own snapshot & `saved/fonts` is shared. `--fresh`
  wipes it first.
- `flexsvc -a NAME run --perf ...`: has the machine to itself (waits for builds & other runs, validation off). Only use
  it for timing.
- `flexsvc -a NAME bench [--seconds 45] [--runs 3] [--baseline r-12]`: the `--play` benchmark (it walks, glides &
  dives in a new world from a fixed seed). Prints p50/p99/max, hitches & top scopes, and the delta against a baseline
  build. Build ids are printed by `build`, and the last 40 are kept. A single run is noisy (p99 can move about 20%), so
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

## Feature scenes & screenshot diffs
`--test-scenes` (no names) runs every scene in `resources/scenes/default/`. Old scenes (stress_test, sponza, shadow_test,
prop_test, donut, ...) live in `resources/scenes/archive/`, which it never lists; copy one back to load it. A scene file
may set `"time of day"` (0 = noon, 0.5 = midnight) & `"pause time of day"`, and `--screenshots` hides the editor UI and
physics lines, so shots only contain the scene.
Each `scene_ft_*` scene tests one feature with a fixed camera & time of day (yaw 0 looks along +x, -pi/2 along -z):
`ft_shadows` (sun cascades), `ft_lights` (point & spot at night), `ft_pbr` (materials), `ft_sky` & `ft_sky_dusk`
(sky/clouds/fog), `ft_particles` (campfire), `ft_transparency` (tinted opaque glass; no translucent pbr material exists yet), `ft_water` (gerstner waves), `ft_terrain`;
plus `scene_macbeth` (colour pipeline) & `scene_terminal_test` (text/UI). To find what broke a feature:
```
flexsvc -a NAME run -- --test-scenes --screenshots --frames=300 scene_ft_shadows.json scene_ft_lights.json scene_ft_pbr.json scene_ft_sky.json scene_ft_sky_dusk.json scene_ft_particles.json scene_ft_transparency.json scene_ft_water.json scene_ft_terrain.json scene_macbeth.json scene_terminal_test.json
flexsvc -a NAME shots ref features     # before your change
flexsvc -a NAME shots diff features    # after: only the broken feature's scene(s) differ
```
Terrain needs about 300 frames to settle, and water/particles/clouds animate, so expect
small noise there.

## When the tree is broken or you're waiting
- Keep shared files compiling at every save. Stage a big rewrite in a copy (in your scratch dir) until it compiles, or
  bracket a multi-file edit with `flexsvc -a NAME hold` / `release`: builds wait (max 90 s, holds expire after 10 min)
  so a header and its .cpp land in the same build.
- `build --mine` compile-checks only your claimed `.cpp` files (Linux; about a minute) before you save into shared
  ones. A failed `build` says how many errors are in files you claimed; if none, it's probably not yours (unless a
  header you changed caused it).
- `run --last-good` runs the newest passing build (and lists which of your claimed files changed after it).
  `run --build r-12` runs any kept build with the shaders it was built with (each build snapshots `saved/spv`).
  Scene/config files are still read live, so pin content yourself if a comparison needs it. `pin r-12` stops a build
  being pruned (40 are kept).
- Don't poll by hand. `build --until-ok --max 100` re-builds as sources change until it passes. `run --detach ... `
  returns a ticket; `flexsvc -a NAME wait TICKET [--max 100]` prints the output (exit 3 = still running).
- A build warns when sources changed while it compiled (objects may be stale; the changed files are touched so the
  next build recompiles them). Rebuild before trusting a crash.
- `bench --env K=V --baseline same --baseline-env K=V` A/Bs a toggle in one build (the valid comparison after a shader
  change). `run --grep RE` prints only matching lines.
- Scratch prints: start them with `DBG:`. `run` summaries ignore those lines, but delete them before you finish.
- Windows may briefly deny writing a .cpp (MSBuild or a virus scanner holds it): just retry.
- Normal runs set `FLEX_NO_FOCUS=1`, so the window opens without taking focus or your mouse/keyboard (`FLEXSVC_FOCUS=1`
  opts out). `--perf` runs & `bench` take focus: an unfocused window measured ~2x the hitches & +4 ms p99. Timing a
  normal run is only good for rough numbers.
- GPU slots (`FLEXSVC_GPU_SLOTS`, default 3) only cap concurrent runs; they aren't per GPU. With one GPU the limit is
  usually CPU, and a Linux build slows runs a lot (it now runs `nice`d at half the cores). Don't take timings during one.
- Runs in one sandbox take turns (they share its `saved/`, exe & shaders), so a second `run` waits for the first.
- Lock waits name the holder & how long they've held it. A `--perf` run or `bench` holds the whole machine for minutes,
  so if you're stuck behind one, `run --detach` and keep working instead of waiting in the foreground.
- `flexsvc -a NAME kill` stops your sandbox's Flex.exe *and* your other flexsvc commands (stuck runs/waits). Use it
  instead of `pkill -f "flexsvc -a NAME"`, which matches & kills your own shell.
- After a crash or restart, `flexsvc status` lists each agent's claimed sources edited since the last good build and
  all source files edited since then: check those half-finished edits first.
- `--test-scenes` & `--play=` runs write stdout unbuffered, so a run that timed out still logs up to where it hung.
  Raise `--timeout` for long scene lists (each scene loads separately; 5+ scenes can exceed the default 300s).

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
CHANGED: one line for each change
SHARED: new APIs / shared behaviour changes (also posted on the board)
VERIFIED: what you ran (command, build id) & the result; numbers if perf
NOT VERIFIED: what you couldn't check (e.g. visuals you didn't look at)
FOLLOW-UP: what the user needs to know, important things only
```
The report is your recap: don't write a commit message. The manager writes one for the whole dispatch. The manager
checks your report against your diff & screenshots, so list exactly what you changed & ran. It may send you follow-up
work afterwards; you keep your context.

# Dispatching
The manager's playbook (planning, briefs, monitoring, wrap-up & the commit message) is the `dispatch` skill:
`.claude/skills/dispatch/SKILL.md`.
