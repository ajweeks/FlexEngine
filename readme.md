# Flex Engine

[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

This is a rendering engine which supports both Vulkan and OpenGL. The goal of this project is to learn about each API and have a test bed for future demos/games.

By supporting three different graphics APIs, I've challenged myself to designed the renderer interface in an API-agnostic way, such that any code calling into the renderer knows nothing about which API is being used.

#### Notable features
- Support for both Vulkan and OpenGL, switchable at runtime
- Physically Based Rendering (PBR)
- Image Based lighting
- Diffuse/specular/normal mapping
- UI interface (using [ImGui](https://github.com/ocornut/imgui))
- Sky boxes

<div>
  <img src="http://i.imgur.com/KOKpDgO.png" width="51%"/>
  <img src="http://i.imgur.com/OVFW6s3.png" width="47%" style="float: right"/>
</div>

![](http://i.imgur.com/weOiqnU.png)

![](http://i.imgur.com/WCios65.png)

![](http://i.imgur.com/NoiEoNY.png)

## Dependencies
 - [Assimp](https://github.com/assimp/assimp)
 - [glad](https://github.com/Dav1dde/glad)
 - [glfw](https://github.com/glfw/glfw)
 - [glm](https://github.com/g-truc/glm)
 - [stb](https://github.com/nothings/stb)
 - [ImGui](https://github.com/ocornut/imgui)

## Thanks
A huge thanks to the following people for their incredibly useful resources:
 - Alexander Overvoorde of [vulkan-tutorial.com](https://vulkan-tutorial.com/)
 - Sascha Willems of [github.com/SaschaWillems/Vulkan](https://github.com/SaschaWillems/Vulkan)
 - Baldur Karlsson of [github.com/baldurk/renderdoc](https://github.com/baldurk/renderdoc)
 - Joey de Vries of [learnopengl.com](https://learnopengl.com/)
