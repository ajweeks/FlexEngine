# Flex Engine

[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)
<!-- [![Build status](https://ci.appveyor.com/api/projects/status/vae2k07y8a802odn?svg=true)](https://ci.appveyor.com/project/ajweeks/flexengine) -->

Flex Engine is a personal game engine I began work on in February 2017. I use it as a playground for learning about real-time technniques. I try to keep [master](https://github.com/ajweeks/FlexEngine/tree/master) reasonably stable, and therefore update it infrequently. See [development](https://github.com/ajweeks/FlexEngine/tree/development) for the latest changes.

#### Notable Features
- Vulkan and OpenGL backends
- Signed-distance field font generation & rendering
- Physically based shading model
- Image based lighting
- Screen-space ambient occlusion
- Stable cascaded shadow mapping
- Conditional checksum-based shader compilation
- Scene editor with serialization
- Profiling tools
- In-game scripting language

![](FlexEngine/screenshots/2018-07-08_21-52-09.png)

![](FlexEngine/screenshots/2019-06-23_11-21-10.jpg)
**Basic implementation of Cascaded Shadow Mapping**

![](FlexEngine/screenshots/2018-07-10_profiling-visualization-06.jpg)
**Profiler overlay showing a breakdown the CPU-time of a single frame**

![](FlexEngine/screenshots/2019-04-21_imgui.jpg)
**Some of the editor windows**

![](FlexEngine/screenshots/2019-05-26_21-05-27.png)
**Screen-Space Ambient Occlusion (SSAO)**

![](FlexEngine/screenshots/2017-10-19_16-17-00-G-Buffer.jpg)
**GBuffer (top-left to bottom-right):** position, albedo, normal, final image, depth, metallic, AO, roughness

![](FlexEngine/screenshots/2017-10-08_11-38-06-combined.jpg)

![](FlexEngine/screenshots/2017-10-08_10-46-22-combined.jpg)

![](FlexEngine/screenshots/2017-10-08_10-33-45-combined.jpg)

<div style="display: inline-block; padding-bottom: 20px">
  <img src="FlexEngine/screenshots/2017-10-08_14-35-01.png" width="49%"/>
  <img src="FlexEngine/screenshots/2017-10-08_14-41-35.png" width="49%" style="float: right"/>
</div>

![](FlexEngine/screenshots/2017-10-08_10-41-01_360_edited.gif)

See more screenshots [here](https://github.com/ajweeks/FlexEngine/tree/development/FlexEngine/screenshots)

## Building Flex (Windows-only)
If you want to build Flex Engine on your own system, follow these steps. You an always download the latest release binaries [here](https://github.com/ajweeks/flexengine/releases) if that's what you're after.

#### Requirements:
- Visual Studio 2015 (or later)
- [GENie](https://github.com/bkaradzic/GENie)

#### Steps
1. Recursively clone the repository to get all submodules
2. Ensure [GENie](https://github.com/bkaradzic/GENie) is either on your PATH, or `genie.exe` is in the `scripts/` directory
3. Run `scripts/generate-vs-*-files.bat` (this simply runs the command `genie vs2015` or `genie vs2017`)
4. Open `build/FlexEngine.sln`
5. Build and run!

## Dependencies
Flex Engine uses the following open-source libraries:
 - [Bullet](https://github.com/bulletphysics/bullet3) - Collision detection & rigid body simulation
 - [FreeType](https://www.freetype.org/) - Font loading
 - [glad](https://github.com/Dav1dde/glad) - OpenGL profile loading
 - [glfw](https://github.com/glfw/glfw) - Window creation, input handling
 - [glm](https://github.com/g-truc/glm) - Math operations
 - [ImGui](https://github.com/ocornut/imgui) - User interface
 - [OpenAL](https://www.openal.org) - Audio loading and playback
 - [stb](https://github.com/nothings/stb) - Image loading
 - [cgltf](https://github.com/jkuhlmann/cgltf) - Mesh loading

## License
Flex engine is released as open source under The MIT License. See [license.md](license.md) for details.

## Acknowledgements
A huge thank you must be given to the following individuals and organizations for their incredibly useful resources:
 - Baldur Karlsson of [github.com/baldurk/renderdoc](https://github.com/baldurk/renderdoc)
 - Alexander Overvoorde of [vulkan-tutorial.com](https://vulkan-tutorial.com)
 - Sascha Willems of [github.com/SaschaWillems/Vulkan](https://github.com/SaschaWillems/Vulkan)
 - Joey de Vries of [learnopengl.com](https://learnopengl.com)
 - Andrew Maximov for the pistol model and textures [artisaverb.info/PBT.html ](http://artisaverb.info/PBT.html)
 - [FreePBR.com](https://FreePBR.com) for the high-quality PBR textures
 - All authors and contributors to the open-source libraries mentioned above

## Blog
 Stay (somewhat) up to date about this project on my blog at [ajweeks.com/blog](https://ajweeks.com/blog/)
