# Working on Flex

General lessons for working in this repo (not task specifics).

## Building & testing
- Windows Release is the build that matters for performance. Build with MSBuild (`build/Flex.sln`, `/p:Configuration=Release /p:Platform=x64`) and run `bin/Release_x64/FlexEngine/Flex.exe` from WSL via interop; it uses the real GPU. The Linux build (`build/debug64`, `ninja`) renders on llvmpipe, so only use it for compile checks.
- MSVC and GCC disagree. Code that compiles on Linux can still fail on Windows, so always do a Windows build before handing off.
  - `include/ThirdParty/sse_mathfun.h` `#define`s `inline` on MSVC. Any standard header first included after it breaks. Add new std headers to `include/stdafx.hpp`, not to engine headers.
- `--test-scenes` only runs 10 frames per scene by default. Streaming/async systems need `--frames=1500` or more, or they never finish loading and their stats are never printed.
- Don't use `pkill -f <pattern>` from a shell whose own command line contains the pattern, because it kills itself. Use `pgrep -x` + `kill`.
- Disk caches (`FlexEngine/saved/`) persist between test runs. A short run can save a partial cache that skews the next run's numbers, so check which run you are measuring.
- ImGui here is an old version: no `ImGuiSliderFlags_*` (use the float `power` argument) and no `ImGuiMouseButton_*` (use integers).
- `stdafx.hpp` uses CRLF; most other files use LF. Text-mode edits with `sed` `$` anchors silently fail on CRLF files, so verify that edits actually applied.
- Shaders can be validated with `/mnt/c/VulkanSDK/*/Bin/glslangValidator.exe -V`. Inline the `#include`s first and run it on a copy under `/mnt/c/...`, since the Windows exe can't read WSL paths.
- Release builds have no shader compiler: they load `FlexEngine/saved/spv/<name>_<stage>.spv`. After editing shaders, run a Debug build once or compile them yourself (glslang, then `spirv-opt -O`). Unoptimized SPIR-V keeps dead texture samples, which trigger validation errors.
- The user's Windows environment sets `VK_INSTANCE_LAYERS` to the validation layer, so even Release runs are validated (and their CPU timings are inflated about 3x). For perf numbers, run through a `.bat` that clears it.
- `--test-scenes --profile` prints CPU & GPU timings in any build. `--screenshots` saves each scene's last frame, which is useful for catching broken rendering.
- Without arguments Flex starts in the game (main menu, `scene_world.json`); `--editor` opens the last test scene instead. `FLEX_GAME_TOUR=1` drives every game screen and saves `saved/screenshots/tour_*.png` (pass env vars to Flex.exe from WSL with `WSLENV`). `FLEX_DEBUG_OVERLAY=<n>` sets the debug overlay (terrain shows sky visibility as "metallic", 4). `FLEX_TEST_FIRST_PERSON`, `FLEX_TEST_SHOW_INVENTORY` & `FLEX_TRAVERSAL_DEBUG` help with scene tests.
- Console builds wait in `system("pause")` after shutdown (except in game mode). Redirect stdin from `/dev/null` when running them from scripts, or a run can look like it hangs on exit.

## Performance investigations
- Measure before guessing. Add timing or counters (and print them in existing load/stats output) rather than reasoning about which call "must" be slow. Several plausible culprits turned out to be wrong; the real ones were in unrelated shared code.
- Compare against a baseline. When a metric looks bad, run the same scenario on the previous version (e.g. `git show :path` into a scratch dir) before changing things.
- Colour pipeline (see the top of `vk_misc.glsl`): material colours & lighting are linear; lit shaders end with `LinearToDisplay` (PBR Neutral tone map + sRGB) & write display values; sky/fog/clouds/unlit shaders work in display space; post only grades. Don't add another tone map or gamma step. `scene_macbeth.json` (24 ColorChecker patches unlit, lit & textured) checks it: the unlit patches must read back as their exact sRGB values.
- The CPU packs a shader's uniforms tightly in `DECL_UNIFORM` declaration order (not `AddUniform` order) & without std140 padding, so a GLSL UBO must declare them in that order with no vec4 at a non-16-byte offset. Don't size UBO arrays with spec constants either (it moves the members after them).
- Uniforms written per draw must use separate dynamic UBO slots: every draw is recorded before any execute, so reusing one slot means all draws see the last values (this garbled text whenever two fonts were on screen).
- Renderer hot spots seen so far:
  - Undersized GPU buffers that grow repeatedly. Reading back host-visible (write-combined) memory is very slow.
  - Per-object work that recreates shared state (descriptor sets).
  - Linear scans over all render objects.
- Small standalone harnesses (compile a subsystem's .cpp with a tiny shim `stdafx.hpp`) are much faster to iterate with than the full engine. They can check invariants (e.g. crack-free LOD seams, consistent neighbor masks) far more thoroughly.

## Code layout
- Game object types live in `Scene/Objects/*` (one file per family). New types must be added to the table in `Scene/GameObjectTypes.cpp`, which is the factory, the property collection registration & the serialization flags, and their names to `config/game_object_types.txt` (CRLF), or they serialize without a type.
- New source files need the projects regenerated: `scripts/genie --file=genie.lua ninja` & `scripts/genie.exe --file=genie.lua vs2026` (run from `scripts/`).
- `Game` (`src/Game.cpp`) owns the menus, worlds (`saved/worlds/<name>/`, each with its own terrain edits) & settings; `GameUI` is the immediate mode UI toolkit it draws with.
- World saves (`world.bin`) & terrain edits use the tagged binary container in `SaveFile.hpp` (sections, crc, zlib, atomic writes with a `.bak`). Fields added to the world's JSON object in `Game::SaveWorld` are saved automatically (small ones in the `META` section that world listing reads). Old `world.json` saves are still read & migrated on the next save. `--test` runs its round-trip & corruption tests (`FLEX_SAVE_BENCHMARK=1` adds timings).
- Per-frame parallel work uses `JobSystem::Dispatch` + `Wait`. Long running background work (terrain generation) has its own pool at background priority. Don't spawn threads directly.
- Player movement abilities are in `Traversal` (owned by `Player`). When it controls the player's velocity, `PlayerController` skips walking.
- Player-facing text lives in `resources/loc/en.txt` (`Key = Text`), not in source. To add a string, add a line there & use `LOC(Key)` (or `LOCF(Key, args...)` for printf-style text); keys aren't declared anywhere in code. Each call site resolves its key once, so it's cheap every frame. Missing keys show as the key & are logged once. The files hot-reload while the game runs.
- Keys the game reads directly (not through an `Action` binding) are named in `GameControls.hpp` (`GameKeys::`). Use those so the Controls screen (`Game::DrawControls`) stays right, & add new controls to it.

## Design pitfalls
- Async/batched systems: watch for livelock. Restarting in-flight work when inputs change can mean nothing ever completes, so prefer finishing a batch and predicting ahead instead.
- Keep noise and density functions continuous. Conditional early-outs that skip a term can introduce discontinuities; only skip when the result is provably identical, or where it can't affect surfaces.
- Deterministic generation: derive every random choice from the one world seed (via a derive/hash function). Never use `rand()` or time-based seeds in generation code.

## Collaboration
- Other agents and the user edit this repo concurrently, and changes may get staged by someone else. Never revert changes you didn't make; check `git status`/`git diff` before assuming a failure is yours.
- Don't commit unless asked. Work in the main tree (no worktrees).
- The user prefers long autonomous work sessions. Rebuild Release yourself at the end, and report measured numbers plus anything you couldn't verify visually.
