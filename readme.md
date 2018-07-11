# Flex Engine

[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)
<!-- [![Build status](https://ci.appveyor.com/api/projects/status/vae2k07y8a802odn?svg=true)](https://ci.appveyor.com/project/ajweeks/flexengine) -->

Flex Engine is my personal game engine which I began work on in February 2017 as a playground for learning about real-time technology. During the first year of development I focused on the renderer, after which I began adding support for the plethora of other necessary systems that a game engine requires.

#### Notable Features
- Support for both Vulkan and OpenGL, switchable at runtime
- Signed-distance field font generation & rendering
- Physically Based Rendering (PBR)
- Image Based lighting (IBL)
- Reflection probes
- Post-processing stage
- Custom scene file format
- Scene editor
- Physics simulation
- Audio playback

![](http://i.imgur.com/3XQGcDD.png)

![](https://i.imgur.com/DYpYMhH.jpg)
**Profiler overlay showing a breakdown the CPU-time of a single frame**

![](https://i.imgur.com/LbRIVav.jpg)
**GBuffer (top-left to bottom-right):** position, albedo, normal, final image, depth, metallic, AO, roughness

![](http://i.imgur.com/qtP8Mmm.png)

![](http://i.imgur.com/oSIsXt7.png)

![](http://i.imgur.com/KBCXvKs.png)

<div style="display: inline-block; padding-bottom: 20px">
  <img src="http://i.imgur.com/ACLLZ5B.png" width="49%"/>
  <img src="http://i.imgur.com/e0mKpDX.png" width="49%" style="float: right"/>
</div>

![](http://i.imgur.com/mqszTPr.gif)

## Building Flex (Windows-only)
If you want to build Flex Engine on your own system, follow these steps. You an always download the latest release binaries [here](https://github.com/ajweeks/flexengine/releases) if that's what you're after.

#### Requirements:
- Visual Studio 2015
- [GENie](https://github.com/bkaradzic/GENie)

#### Steps
1. Recursively clone the repository using your method of choice (SourceTree, git bash, ...)
2. Ensure [GENie](https://github.com/bkaradzic/GENie) is either on your PATH, or `genie.exe` is in the `scripts/` directory
3. Run `generate-vs-files.bat` (this simply runs the command `genie vs2015`)
4. Open `build/FlexEngine.sln`
5. Build and run!

## Dependencies
Flex Engine uses the following open-source libraries:
 - [Assimp](https://github.com/assimp/assimp) - Model loading
 - [ImGui](https://github.com/ocornut/imgui) - User interface
 - [stb](https://github.com/nothings/stb) - Image loading
 - [glfw](https://github.com/glfw/glfw) - Window creation, input handling
 - [glad](https://github.com/Dav1dde/glad) - OpenGL profile loading
 - [glm](https://github.com/g-truc/glm) - Math operations

## License
Flex engine is released as open source under The MIT License. See [license.md](license.md) for details.

## Acknowledgements
A huge thanks to the following people/organizations for their incredibly useful resources:
 - Alexander Overvoorde of [vulkan-tutorial.com](https://vulkan-tutorial.com/)
 - Sascha Willems of [github.com/SaschaWillems/Vulkan](https://github.com/SaschaWillems/Vulkan)
 - Baldur Karlsson of [github.com/baldurk/renderdoc](https://github.com/baldurk/renderdoc)
 - Joey de Vries of [learnopengl.com](https://learnopengl.com/)
 - Andrew Maximov for the pistol model and textures [artisaverb.info/PBT.html ](http://artisaverb.info/PBT.html)
 - [FreePBR.com](http://FreePBR.com) for the wonderful PBR textures
 - All of the library authors/contributors for their awesome OSS!

## Blog
 Stay (somewhat) up to date about this project on my blog at [ajweeks.wordpress.com/tag/flex-engine/](https://ajweeks.wordpress.com/tag/flex-engine/).
