![](FlexEngine/screenshots/flex_engine_banner_3.png)

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
- GPU particles (compute shader)

![](FlexEngine/screenshots/2018-07-08_21-52-09.png)

![](FlexEngine/screenshots/2019-06-23_11-21-10.jpg)
**Basic implementation of Cascaded Shadow Mapping**

![](FlexEngine/screenshots/2019-11-17-gpu-particles-07.jpg)
**Two million particles updated and rendered entirely on the GPU, utilizing the compute stage**

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

## Building Flex
### Windows
If you want to build Flex Engine on your own system, follow these steps. You an always download the latest release binaries [here](https://github.com/ajweeks/flexengine/releases) if that's what you're after.

#### Requirements:
- Visual Studio 2015 (or later)
- [GENie](https://github.com/bkaradzic/GENie)

#### Steps
1. Recursively clone the repository to get all submodules
2. Ensure [GENie](https://github.com/bkaradzic/GENie) is either on your PATH, or `genie.exe` is in the `scripts/` directory
4. Build GLFW:
    `cd FlexEngine\dependencies\glfw`
    `mkdir build && cd build`  --   -DGLFW_VULKAN_STATIC=ON --  -DCMAKE_CXX_FLAGS="-m32" -DCMAKE_C_FLAGS="-m32"
    `cmake -DGLFW_BUILD_EXAMPLES=false -DGLFW_BUILD_TESTS=false -DGLFW_BUILD_DOCS=false -G"Visual Studio 16 2019" ..`
    `start GLFW.sln`
    Build ALL_BUILD (`F7`)
    `copy src\Debug\glfw3.lib ..\..\..\lib\x64\Debug\`
5. Build OpenAL:
    `cd FlexEngine\dependencies\openAL\build`
    `cmake -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -G"Visual Studio 16 2019 ..`
    `start OpenAL.sln`
    Build ALL_BUILD (`F7`)
    `copy Debug\common.lib ..\..\..\lib\x64\Debug\`
6. Build Bullet:
    `mkdir build && cd build`
    `cmake -DUSE_MSVC_RUNTIME_LIBRARY_DLL=ON -DBUILD_UNIT_TESTS=OFF -DBUILD_CPU_DEMOS=OFF -DBUILD_BULLET2_DEMOS=OFF -DBUILD_EXTRAS=OFF -G"Visual Studio 16 2019" ..`
    `start BULLET_PHYSICS.sln`
    Build ALL_BUILD (`F7`)
    `copy lib\Debug\BulletCollision_Debug.lib ..\..\..\lib\x64\Debug\`
    `copy lib\Debug\BulletDynamics_Debug.lib ..\..\..\lib\x64\Debug\`
    `copy lib\Debug\LinearMath_Debug.lib ..\..\..\lib\x64\Debug\`
7. Build freetype:
    `cd FlexEngine\dependencies\freetype`
    `start builds\windows\vc2010\freetype.sln`
    Target `Debug Static` & `x64`
    Build freetype (`F7`)
    `copy "objs\x64\Debug Static\freetype.lib" ..\..\lib\x64\Debug\`
    `copy "objs\x64\Debug Static\freetype.pdb" ..\..\lib\x64\Debug\`
3. (from root dir) `genie --file=scripts/genie.lua vs2019`
4. Open `build/FlexEngine.sln`
5. Build and run!

# (WIP! These steps will be reconciled into a single job at some point soon(tm))
### Linux (tested only on Ubuntu 18.04, enter at your own risk)
#### Steps
1. Recursively clone the repository to get all submodules
2. Ensure you have the g++ prerequisites:
  `sudo apt-get install g++-multilib`
2. Install [GENie](https://github.com/bkaradzic/GENie) from  https://github.com/bkaradzic/bx/raw/master/tools/bin/linux/genie
3. From the root directory run `genie --file=scripts/genie.lua ninja`, replacing ninja with cmake, gmake, or any other preferred supported build system.
4. Clone and compile openAL in a sibling directory:
  `git clone git://repo.or.cz/openal-soft.git`
  `sudo apt-get install libasound2-dev libpulse-dev`
  `cd openal-soft `
  `mkdir build && cd build`
  `cmake -DALSOFT_EXAMPLES=false -DALSOFT_TESTS=false -DCMAKE_CXX_FLAGS="-m32" -DCMAKE_C_FLAGS="-m32" -DCMAKE_EXE_LINKER_FLAGS="-m32" ..`
  `cd .. && make`
5. Install python dev tools as a prerequisite to Bullet
  `sudo apt-get install python3-dev`
6. Clone and compile bullet:
  `cd bullet`
  `cmake -DCMAKE_CXX_FLAGS="-m32" -DCMAKE_C_FLAGS="-m32" -DBUILD_BULLET2_DEMOS=false -DBUILD_CPU_DEMOS=false -DBUILD_UNIT_TESTS=false .`
  `make`
7. Compile GLFW:
  Install X11 libs:
    `sudo apt-get install xserver-xorg-dev:i386`
  `cd glfw`
  32 bit:
      `cmake -DGLFW_VULKAN_STATIC=ON -DCMAKE_CXX_FLAGS="-m32" -DCMAKE_C_FLAGS="-m32" -DGLFW_BUILD_EXAMPLES=false -DGLFW_BUILD_TESTS=false -DGLFW_BUILD_DOCS=false -G"Unix Makefiles" ..`
      `make`
  64 bit:
      `cmake -DGLFW_VULKAN_STATIC=ON -DGLFW_BUILD_EXAMPLES=false -DGLFW_BUILD_TESTS=false -DGLFW_BUILD_DOCS=false -G"Unix Makefiles" ..`
      `make`
8. Install the Vulkan SDK:
  `wget -qO - http://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -`
  `sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.2.131-bionic.list http://packages.lunarg.com/vulkan/1.2.131/lunarg-vulkan-1.2.131-bionic.list`
  `sudo apt update`
  `sudo apt install vulkan-sdk`
10. Compile FreeType:
  Download freetype-2.10.0.tar.bz2 from https://download.savannah.gnu.org/releases/freetype/
  Unzip
  10. a. Compile libpng as 32 bit:
    Clone libpng from https://downloads.sourceforge.net/libpng/libpng-1.6.37.tar.xz
    Unzip
    `./configure --disable-static`
    Modify `libpng-1.6.37/Makefile`:
      Replace `-lz` with `-l:libz.so.1`
      Add `-m32` to CFLAGS, CPPFLAGS, and LDFLAGS
      `make`
    10. b. Copy libz.so.* from `/lib/i386-linux-gnu/libz.so.1` to freetype root dir (TODO: Set `SYSTEM_ZLIB` instead)
  Modify `builds/unix/unix-cc.mk:102` to specify lib names exactly  (`-l:libz.so.1 -l:libpng16.so.16` instead of `-lz -lpng16`)
  `./configure CFLAGS=-m32 LDFLAGS=-m32 CPPFLAGS=-m32 LT_SYS_LIBRARY_PATH=/home/aj/Dowloads/` (pointing to your local 32 bit build of zlib)
10. Run `make` in the root directory of the project.

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
