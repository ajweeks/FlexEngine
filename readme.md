# Flex Engine

[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

Flex Engine is my personal rendering engine which currently supports Vulkan and OpenGL. It was started in February 2017 as a way for me to better understand how graphics engines work.

#### Notable Features
- Support for both Vulkan and OpenGL, switchable at runtime
- Physically Based Rendering (PBR)
- Image Based lighting (IBL)
- Irradiance map generation
- Reflection probes
- Post-processing stage

#### Planned Features
 - Mesh animation
 - Cascading shadow maps
 - ...

![](http://i.imgur.com/qtP8Mmm.png)

![](http://i.imgur.com/oSIsXt7.png)

![](http://i.imgur.com/KBCXvKs.png)

<div style="display: inline-block; padding-bottom: 20px">
  <img src="http://i.imgur.com/ACLLZ5B.png" width="49%"/>
  <img src="http://i.imgur.com/e0mKpDX.png" width="49%" style="float: right"/>
</div>

![](http://i.imgur.com/mqszTPr.gif)

![](https://i.imgur.com/LbRIVav.jpg)
**GBuffer (top-left to bottom-right):** position, albedo, normal, final image, depth, metallic, AO, roughness


## Dependencies
 - [Assimp](https://github.com/assimp/assimp) - Model loading
 - [ImGui](https://github.com/ocornut/imgui) - User interface
 - [stb](https://github.com/nothings/stb) - Image loading
 - [glfw](https://github.com/glfw/glfw) - Window creation, input handling
 - [glad](https://github.com/Dav1dde/glad) - OpenGL profile loading
 - [glm](https://github.com/g-truc/glm) - Math operations

## Thanks
A huge thanks to the following people/organizations for their incredibly useful resources:
 - Alexander Overvoorde of [vulkan-tutorial.com](https://vulkan-tutorial.com/)
 - Sascha Willems of [github.com/SaschaWillems/Vulkan](https://github.com/SaschaWillems/Vulkan)
 - Baldur Karlsson of [github.com/baldurk/renderdoc](https://github.com/baldurk/renderdoc)
 - Joey de Vries of [learnopengl.com](https://learnopengl.com/)
 - Andrew Maximov for the pistol model and textures [artisaverb.info/PBT.html ](http://artisaverb.info/PBT.html)
 - [FreePBR.com](http://FreePBR.com) for the wonderful PBR textures

## Blog
 Stay up to date about this project on my blog at [ajweeks.wordpress.com/tag/flex-engine/](https://ajweeks.wordpress.com/tag/flex-engine/)
