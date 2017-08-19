# Flex Engine

[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

This is a rendering engine which supports Vulkan 1.0, OpenGL 4, and Direct3D 11. The goal of this project is to learn about each API and have a test bed for future demos/games.

By supporting three different graphics APIs, I've challenged myself to designed the renderer interface in an API-agnostic way, such that any code calling into the renderer knows nothing about which API is being used.


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
