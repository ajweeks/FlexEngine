# Graphics API Tech Demo

<a href="https://scan.coverity.com/projects/ajweeks-pg4_techdemo">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/12767/badge.svg"/>
</a>

This is a rendering engine which supports Vulkan 1.0, OpenGL 4, and Direct3D 12. The goal of this project is to learn about each API and have a test bed for future demos/games.

By supporting three different graphics APIs, I've challenged myself to designed the renderer interface in an API-agnostic way, such that any code calling into the renderer knows nothing about which API is being used.

![](http://i.imgur.com/nGUDNkJ.png)

-AJ Weeks
