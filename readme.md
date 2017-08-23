# Flex Engine

[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

This is a rendering engine which supports both Vulkan and OpenGL. The goal of this project is to learn about each API and have a test bed for future demos/games.

By supporting three different graphics APIs, I've challenged myself to designed the renderer interface in an API-agnostic way, such that any code calling into the renderer knows nothing about which API is being used.

#### Notable features
- API-agnostic rendering interface
- Sky boxes
- Diffuse/specular/normal mapping
- Per-object shader/texture selection

<img src="http://i.imgur.com/CLRQ6tC.jpg" width="49%"/>
<img src="http://i.imgur.com/sXbc0n5.jpg" width="49%" style="float: right"/>

![](http://i.imgur.com/mz4mlmE.jpg)

![](http://i.imgur.com/pb8KjRA.png)

![](http://i.imgur.com/uRPPjTa.png)

## Dependencies
 - [assimp](https://github.com/assimp/assimp)
 - [glad](https://github.com/Dav1dde/glad)
 - [glfw](https://github.com/glfw/glfw)
 - [glm](https://github.com/g-truc/glm)
 - [soil](https://github.com/kbranigan/Simple-OpenGL-Image-Library)
 - [stb](https://github.com/nothings/stb)

## Thanks
A huge thanks to the following people for their incredibly useful resources:
 - Alexander Overvoorde of [vulkan-tutorial.com](https://vulkan-tutorial.com/)
 - Sascha Willems of [github.com/SaschaWillems/Vulkan](https://github.com/SaschaWillems/Vulkan)
 - Baldur Karlsson of [github.com/baldurk/renderdoc](https://github.com/baldurk/renderdoc)
 - Joey de Vries of [learnopengl.com](https://learnopengl.com/)

## Backlog
You can see a list of features I'm planning on adding [here](/backlog.md)
