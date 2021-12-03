
### Flex Engine change log (reverse chronological order)

**2021**
_December_
- Optimize Transform class (cut size from 256B to 120B)
- Make miner's inventories accessible

_November_
- Add keybinding for dropping items (V)
- Add speaker object which can play positional audio and be toggled by the player
- Add support for positional audio sources & configurable reverb
- Add mineral deposits & miners
- Fix prefab creation/deletion/duplication/renaming
- Auto detect modifications of textures/prefab/meshes/audio files

_October_
- Allow player to wear certain items (headlamp)
- Track and optimize GPU memory usage
- Improve startup times by lazily loading textures/materials/descriptor sets

_September_ (0.8.7)
- Display wires as simulated soft body tube
- Refactor transform system & game loop (add fixed update)

_August_
- Show preview item placement
- Support holding an item in each hand
- Serialize player inventory

_July_
- Rewrite terrain generator to use marching cubes algorithm

_June_
- Replace home-brewed profiler with [palanteer](https://github.com/dfeneyrou/palanteer/)
- Port terrain generation to GPU (async compute)
- Make shader compilation async again

_May_ (0.8.6)
- Begin adding support for terrain biomes & blending between
- Add support for calling functions with arguments & non-void return types from in-game command line
- Add support for calling C++ functions from script
- Add support for binding function pointers

_April_
- Display interactive inventory screen (tab)
- Add basic UI framework based on Rect Cuts
- Add support for area lights (using Linearly Transformed Cosines)
- Add support for function calls in scripting language
- Support hot reloading externally-edited terminal scripts
- Support reading terminal scripts from file
- Add shader include support
- Add collision to terrain (active zone moves with player)

_March_
- Refactor graphics pipeline & descriptor set creation to be much more efficient
- Optimize & multithread terrain generation
- Make road meet terrain smoothly

_February_
- Add support for road generation
- Add sounds for vehicle motor

_January_
- Add vehicle camera
- Add vehicles
- Reduce memory usage of dynamic uniform buffers by allowing lazy initialization
- Define prefabs using "template" game objects

**2020**
_December_
- Add StringID (hashed string)
- Add GameObjectIDs (GUID)
- Add ResourceManager
- Add first pass at vehicles
- Various editor improvements (scene/object creation/duplication etc.)

_November_
- Add mesh target based coil springs
- Dynamic buffer fixes
- Add soft body type that can be initialized from mesh
- Add first position-based dynamics solver
- Add keybinding editor
- Add plugables system to connect computers to peripherals

_October_ (0.8.5)
- Further work on compiler

_September_
- Further work on compiler

_August_
- Improve scripting language infastructure (diagnostics, intermediate representation)

_July_
- Add mesh drag-drop import support (glb/gltf only)
- Add support for having multiple specialization constants per stage
- Optimize text rendering by keeping text vertex buffer resident
- Move most generated files to FlexEngine/saved/

_June_
- Improve shader/material UI windows
- Add mipmapping support
- Fix font SDF rendering

_May_
- Add wireframe shader
- Add tesselation & sampling LOD levels to water mesh
- Make water generation infinite & vastly more optimial (utilize SIMD & multithreading)

_April_ (0.8.4)
- Fix water generation & center around camera
- Add directory watch to auto-recompile shaders on file modification for lightning-fast iteration times
- Add more granular shader compilation using shaderc library
- Add basic terrain generation/streaming

_March_
- Finish implementing edior gizmos (translate, rotate, scale)
- Finish linux port
- Remove glad
- Add support for 64 bit compilation
- Add volk to handle Vulkan loading

_February_
- Fix remaining linux compilation errors/warnings

_January_
- Fade out gizmos when facing head-on


**2019**
_December_ (0.8.3)
- Add support for multiple material IDs per mesh
- Add unit test framework
- Start porting to linux

_November_
- Add compute-based GPU particles
- Calculate render pass image transitions automatically (mini render graph)

_October_ (0.8.2)
- Wrap render passes in abstraction
- Improve scene management
- Improve TAA implementation

_September_
- Improve RenderDoc API integration
- Improve editor sprite rendering (fade when close, fix billboard issue)

_August_
- Add GPU timings via timestamps

_July_
- Lots of cleanup of Vulkan renderer
- Add first pass of TAA (temporal anti-aliasing)

_June_
- Add Vulkan debug markers
- Add debug preview of cascades
- Add cascaded shadow mapping to GL & Vulkan
- Lots of cleanup, refactoring, and bug fixing

_May_
- Add shadow mapping to Vulkan
- Add screen-space ambient occlusion to Vulkan & OpenGL
- Remove world position from GBuffer, reconstruct from depth instead

_April_ (0.8.1)
- Add async Vulkan shader compilation on bootup (based on shader file checksums)
- Add shader hot-reloading support to Vulkan
- Add font metadata viewer
- Track memory allocations via custom new/delete/malloc/free/etc.
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

_January_ (0.8.0)
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
_December_ (0.7.0)
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
