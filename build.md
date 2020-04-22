![](FlexEngine/screenshots/flex_engine_banner_3.png)

[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

# Building Flex

Note that is process is unoptimal/more painful than it needs to be. Flex isn't intended to be used by anyone but me so proceed at your own risk!

### Windows
If you want to build Flex Engine on your own system, follow these steps. You an always download the latest release binaries [here](https://github.com/ajweeks/flexengine/releases) if that's what you're after.

#### Requirements:
- Visual Studio 2015 or later
- [GENie](https://github.com/bkaradzic/GENie)

#### Steps
1. Recursively clone the repository to get all submodules
2. Ensure [GENie](https://github.com/bkaradzic/GENie) is either on your PATH, or `genie.exe` is in the `scripts/` directory
3. Create libs dir:
  `mkdir FlexEngine/lib/x64/Debug`
4. Build GLFW:
    `cd FlexEngine\dependencies\glfw`
    `mkdir build && cd build`
    `cmake -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF ..`
    `start GLFW.sln`
    Build ALL_BUILD (`F7`)
    `copy /y src\Debug\glfw3.lib ..\..\..\lib\x64\Debug\`
5. Build OpenAL:
    `cd FlexEngine\dependencies\openAL`
    `mkdir build && cd build`
    `cmake -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF ..`
    `start OpenAL.sln`
    Build ALL_BUILD (`F7`)
    `copy /y Debug\common.lib ..\..\..\lib\x64\Debug\`
    `copy /y Debug\OpenAL32.dll ..\..\..\lib\x64\Debug\`
    `copy /y Debug\OpenAL32.lib ..\..\..\lib\x64\Debug\`
    `copy /y Debug\OpenAL32.pdb ..\..\..\lib\x64\Debug\`
6. Build Bullet:
    `mkdir build && cd build`
    `cmake -DUSE_MSVC_RUNTIME_LIBRARY_DLL=ON -DBUILD_UNIT_TESTS=OFF -DBUILD_CPU_DEMOS=OFF -DBUILD_BULLET2_DEMOS=OFF -DBUILD_EXTRAS=OFF ..`
    `start BULLET_PHYSICS.sln`
    Build ALL_BUILD (`F7`)
    `copy /y lib\Debug\BulletCollision_Debug.lib ..\..\..\lib\x64\Debug\`
    `copy /y lib\Debug\BulletDynamics_Debug.lib ..\..\..\lib\x64\Debug\`
    `copy /y lib\Debug\LinearMath_Debug.lib ..\..\..\lib\x64\Debug\`
7. Build freetype:
    `cd FlexEngine\dependencies\freetype`
    `start builds\windows\vc2010\freetype.sln`
    Target `Debug Static` & `x64`
    Build freetype (`F7`)
    `copy /y "objs\x64\Debug Static\freetype.lib" ..\..\lib\x64\Debug\`
    `copy /y "objs\x64\Debug Static\freetype.pdb" ..\..\lib\x64\Debug\`
8. Build shaderc:
  `cd ..`
  `git clone https://github.com/google/shaderc`
  `cd shaderc`
  `python utils/git-sync-deps`
  `mkdir build && cd build`
  `cmake .. -DBUILD_GMOCK=OFF -DBUILD_TESTING=OFF -DENABLE_BUILD_SAMPLES=OFF -DENABLE_CTEST=OFF -DINSTALL_GTEST=OFF -DSHADERC_ENABLE_SHARED_CRT=ON -DLLVM_USE_CRT_DEBUG="MDd" -DLLVM_USE_CRT_MINSIZEREL="MD" -DLLVM_USE_CRT_RELEASE="MD" -DLLVM_USE_CRT_RELWITHDEBINFO="MD"`
  `cmake --build .`
  `copy /y libshaderc\Debug\shaderc_combined.lib ..\..\FlexEngine\FlexEngine\lib\x64\Debug\`
9. (from root dir) `genie --file=scripts/genie.lua vs2019`
10. Open `build/FlexEngine.sln`
11. Target `Debug` & `x64`
12. Build and run!

# (WIP! These steps will be reconciled into a single job at some point soon(tm))
### Linux (tested only on Ubuntu 18.04, enter at your own risk)
#### Steps
1. Recursively clone the repository to get all submodules
2. Ensure you have the g++ prerequisites:
  `sudo apt-get install g++-multilib`
3. Install openAL SDK:
  `sudo apt-get install libopenal-dev`
4. Install python dev tools as a prerequisite to Bullet
  `sudo apt-get install python3-dev`
5. Clone and compile bullet:
  `cd bullet`
  `mkdir build && cd build`
  `cmake -DBUILD_UNIT_TESTS=OFF -DBUILD_CPU_DEMOS=OFF -DBUILD_BULLET2_DEMOS=OFF -DBUILD_EXTRAS=OFF ..`
  `make`
  `cp src/BulletCollision/libBulletCollision.a ../../../lib/x64/Debug/`
  `cp src/BulletDynamics/libBulletDynamics.a ../../../lib/x64/Debug/`
  `cp src/LinearMath/libLinearMath.a ../../../lib/x64/Debug/`
6. Compile GLFW:
  Install X11 libs:
    `sudo apt-get install xserver-xorg-dev libxcursor-dev libxi-dev` (`xserver-xorg-dev:i386` for x32)
    `cd glfw`
  32 bit:
      `cmake -DCMAKE_CXX_FLAGS="-m32" -DCMAKE_C_FLAGS="-m32" -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF -G"Unix Makefiles" .`
      `make`
  64 bit:
      `cmake -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF -G"Unix Makefiles" .`
      `make`
  `cp src/libglfw3.a ../../lib/x64/Debug/`
7. Install the Vulkan SDK:
  `wget -qO - http://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -`
  `sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.2.131-bionic.list http://packages.lunarg.com/vulkan/1.2.131/lunarg-vulkan-1.2.131-bionic.list`
  `sudo apt update`
  `sudo apt install vulkan-sdk`
8. Compile FreeType:
  `cd freetype`
  Install dependencies:
  `sudo apt-get install automake libtool autoconf`
  `sh autogen.sh`
  Build:
  `mkdir build && cd build`
  `cmake -DCMAKE_DISABLE_FIND_PACKAGE_HarfBuzz=ON ..`
  `make`
  `cp libfreetype.a ../../../lib/x64/Debug/`
9. Build shaderc:
  `cd ..`
  `git clone https://github.com/google/shaderc`
  `cd shaderc`
  `./utils/git-sync-deps`
  `mkdir build && cd build`
  `cmake .. -DBUILD_GMOCK=OFF -DBUILD_TESTING=OFF -DENABLE_BUILD_SAMPLES=OFF -DENABLE_CTEST=OFF -DINSTALL_GTEST=OFF -DSHADERC_ENABLE_SHARED_CRT=ON -DLLVM_USE_CRT_DEBUG="MDd" -DLLVM_USE_CRT_MINSIZEREL="MD" -DLLVM_USE_CRT_RELEASE="MD" -DLLVM_USE_CRT_RELWITHDEBINFO="MD" -DSHADERC_SKIP_TESTS=ON`
  `cmake --build .`
  `cp libshaderc/Debug/shaderc_combined.lib ../../FlexEngine/FlexEngine/lib/x64/Debug/`
10. Install [GENie](https://github.com/bkaradzic/GENie) from  https://github.com/bkaradzic/bx/raw/master/tools/bin/linux/genie
11. From the root directory run `genie --file=scripts/genie.lua ninja`, replacing ninja with cmake, gmake, or any other preferred supported build system.
12. Build:
  `ninja -C debug64`
13. Run!
  `./../bin/Debug_x64/FlexEngine/Flex`
