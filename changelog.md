
### Flex Engine change log (reverse chronological order)

**2019**
_May_
- Add shadow mapping to Vulkan
- Add SSAO support in Vulkan & OpenGL
- Remove world position from GBuffer, reconstruct from depth instead

_April_
- Add async Vulkan shader compilation on bootup (based on checksum)
- Add shader hot-reloading support to Vulkan
- Add font metadata viewer
- Batch render objects (editor/gameplay > deferred/forward > shader > material)
- Use negative viewport heights in Vulkan to avoid negation in shader
- Add SDF font generation & rendering support to Vulkan
- Switch to using reverse Z depth buffers

_March_
- Add asynchronous vulkan shader compilation using a shader file contents checksum to only recompile when changes have been made
- Add console command support and several commands (reload scene, reload shaders, ...) (accessed via ~ key)
- Get Vulkan renderer mostly back up and running

_February_
- Add Gerstner waves
- Add abtract syntax tree generator/evaluator
- Add tokenizer
- Add many text editor features
- Add terminals & terminal camera
- Add support for world-space text rendering

_January_
- Add basic keymapping UI (Window > Key Mapper)
- Allow callbacks to be given a priority
- Add input event callbacks
- Add callback functionality
- Add input binding file (`saved/config/input-bindings.ini`)
- Improve compile times (full build: ~45s -> ~25s)
- Add engine carts which drive cart chains
- Make carts that bump into each other connect up into a chain
- Markup OpenGL render passes for easier debugging in RenderDoc
- Save console output to FlexEngine.log periodically
- Replace Assimp with tinygltf


**2018**
_December_
- Add carts that ride along tracks (create with C, place onto nearest track with SPACE)
- Add RenderDoc API integration to allow capturing frames with a keypress (F9)
- Add support for time dilation
- Add support for pausing and single stepping simulation
- Allow player to edit tracks
- Save track data to file
- Save bootup times to file (`saved/bootup-times.log`)
- Fix player direction handling at junctions

_November_
- Allow screenshots to be taken in-game (using numpad 9 key)
- Add basic controls to allow player to place tracks (Y to enter placement mode, RT to place, LT to finalize, X to lock in/out, LT/RT to move along track)
- Add basic keyboard movement inputs
- Bring up rename UI on F2
- Heavily optimize debug rendering
- Add basic sound synthesis (play notes using 4-9 keys)
- Add ability to change overhead camera zoom level (L & R joystick press)
- Improve "overhead" camera to follow player position & rotation
- Allow player to switch direction on a track by holding right joystick in a direction

_October_
- Add tracks (Bezier Curves which the player can move along for speedy travel) (X button to connect/disconnect)
- Separate depth-aware editor objects from depth-unaware
- Triplanar mapping shader
- Prevent input handling when window doesn't have focus
- Add first person controller (with separate pitch for look dir & capsule)
- Add track junctions to handle switching tracks
- Improve transform gizmo implementation (namely rotation & scale)
	- Fade out axes when facing head-on

_Feb 2017 - Sept 2018_
- Unaccounted for :shrug:
