![](FlexEngine/screenshots/flex_engine_banner_3.png)

[![linux](https://github.com/ajweeks/FlexEngine/actions/workflows/build_linux.yml/badge.svg)](https://github.com/ajweeks/FlexEngine/actions/workflows/build_linux.yml)
[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)
[![forthebadge](https://forthebadge.com/images/badges/works-on-my-machine.svg)](https://forthebadge.com)

Flex Engine is a personal game engine I began work on in February 2017. I use it as a playground for learning about real-time techniques. I try to keep [master](https://github.com/ajweeks/FlexEngine/tree/master) reasonably stable, and therefore update it infrequently. See [development](https://github.com/ajweeks/FlexEngine/tree/development) for the latest changes.

#### Notable Features
- Vulkan backend
- Conditional checksum-based shader compilation
- Support for Windows & linux
- Scene editor with serialization
- In-game scripting language & virtual machine
- Built-in profiling capture/inspection tools
- Rendering:
	- Physically based shading model
	- Image based lighting
	- Screen-space ambient occlusion
	- Stable cascaded shadow mapping
	- Temporal anti-aliasing
	- Signed-distance field font generation & rendering
	- GPU particles
	- Terrain, ocean, and sky rendering

![](FlexEngine/screenshots/2021-05-22_21-05-20.jpg)
**Procedural terrain generated on the GPU**

![](FlexEngine/screenshots/2020-06-13_water_04.jpg)
**Gerstner wave ocean simulation**

![](FlexEngine/screenshots/2019-06-23_11-21-10.jpg)
**Cascaded Shadow Mapping**

![](FlexEngine/screenshots/2019-11-17-gpu-particles-07.jpg)
**Two million particles simulated and rendered entirely on the GPU**

![](FlexEngine/screenshots/2018-07-10_profiling-visualization-06.jpg)
**Profiler overlay showing a breakdown the CPU-time of a single frame**

![](FlexEngine/screenshots/2019-04-21_imgui.jpg)
**Some editor windows**

![](FlexEngine/screenshots/2019-05-26_21-05-27.png)
**Screen-Space Ambient Occlusion (SSAO)**

![](FlexEngine/screenshots/2017-10-19_16-17-00-G-Buffer.jpg)
**GBuffer (top-left to bottom-right):** position, albedo, normal, final image, depth, metallic, AO, roughness

![](FlexEngine/screenshots/2018-07-08_21-52-09.png)
**View into editor**

![](FlexEngine/screenshots/2017-10-08_11-38-06-combined.jpg)

![](FlexEngine/screenshots/2017-10-08_10-46-22-combined.jpg)

![](FlexEngine/screenshots/2017-10-08_10-33-45-combined.jpg)

<div style="display: inline-block; padding-bottom: 20px">
  <img src="FlexEngine/screenshots/2017-10-08_14-35-01.png" width="49%"/>
  <img src="FlexEngine/screenshots/2017-10-08_14-41-35.png" width="49%" style="float: right"/>
</div>

![](FlexEngine/screenshots/2017-10-08_10-41-01_360_edited.gif)

See more screenshots [here](https://github.com/ajweeks/FlexEngine/tree/development/FlexEngine/screenshots)

## Building Flex
See `build.md`

## Dependencies
Flex Engine uses the following open-source libraries:
 - [Bullet](https://github.com/bulletphysics/bullet3) - Collision detection & rigid body simulation
 - [FreeType](https://www.freetype.org/) - Font loading
 - [glfw](https://github.com/glfw/glfw) - Window creation, input handling
 - [glm](https://github.com/g-truc/glm) - Math operations
 - [ImGui](https://github.com/ocornut/imgui) - User interface
 - [OpenAL](https://www.openal.org) - Audio loading and playback
 - [stb](https://github.com/nothings/stb) - Image loading
 - [cgltf](https://github.com/jkuhlmann/cgltf) - Mesh loading
 - [volk](https://github.com/zeux/volk) - Vulkan meta-loader

## License
Flex engine is released under The MIT License. See [license.md](LICENSE.md) for details.

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
