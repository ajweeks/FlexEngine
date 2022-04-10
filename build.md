![](FlexEngine/screenshots/flex_engine_banner_3.png)

[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

# Building Flex

If you want to build Flex Engine on your own system, follow these steps. You an always download the latest release binaries [here](https://github.com/ajweeks/flexengine/releases) if that's what you're after.

First ensure you've pulled all the dependencies, either passing `--recurse-submodules` when cloning, or using `git submodule update` after the fact.

Note that prebuilt binaries do exist for linux, see [Pre-built binaries](#1-pre-built-binaries) for steps.

## Windows
#### Requirements:
- Python 3
- cmake 3.13+

#### Steps
1. `cd scripts`
2. `python build_dependencies.py windows vs2019 Debug`
3. Open `build/Flex.sln`
4. Change configuration to Debug x64
5. Build and run!


## Linux
#### Requirements:
- [GENie](https://github.com/bkaradzic/GENie)
- Python 3
- cmake 3.13+

### Ubuntu 18.04
#### Steps
1. Run the following commands to install prerequisites:
  - `sudo apt update`
  - `sudo apt-get install cmake dos2unix g++-multilib libopenal-dev python3-dev xserver-xorg-dev libxcursor-dev libxi-dev libxrandr-dev libxinerama-dev automake libtool autoconf libbz2-dev uuid-dev`
  - `wget -qO - http://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -`
  - `sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.2.131-bionic.list http://packages.lunarg.com/vulkan/1.2.131/lunarg-vulkan-1.2.131-bionic.list` (substitute in any newer vulkan version)
2. `cd scripts`
3. `python3 build_dependencies.py linux gmake Debug`
4. `make`
5. `cd ../bin/Debug_x64/FlexEngine`
6. `./Flex`

### Solus 4.2
#### Steps
1. Run the following commands to install prerequisites:
  - `sudo eopkg upgrade`
  - `sudo eopkg install cmake dos2unix gcc llvm-clang glibc-devel libx11-devel libxcursor-devel vulkan automake libtool autoconf make libxrandr-devel libxinerama-devel libxi-devel openal-soft-devel libpng-devel bzip2-devel`
  - `sudo eopkg install -c system.devel uuid-dev`
  - Install latest vulkan sdk by following steps here: https://vulkan.lunarg.com/sdk/home
2. `cd scripts`
3. `git config --global init.defaultBranchName main`
4. `python3 build_dependencies.py linux gmake Debug`
5. `make`
6. `cd ../bin/Debug_x64/FlexEngine`
7. `./Flex`

### Fedora
#### Steps
1. Run the following commands to install prerequisites:
  - `sudo dnf update`
  - `sudo dnf install cmake dos2unix gcc glibc-devel libx11-devel libXcursor-devel vulkan automake libtool autoconf make libXrandr-devel libXinerama-devel libXi-devel openal-soft libpng zlib uuid-dev`
  - Install latest vulkan sdk by following steps here: https://vulkan.lunarg.com/sdk/home
2. `cd scripts`
3. `git config --global init.defaultBranchName main`
4. `python3 build_dependencies.py linux gmake Debug`
5. `make`
6. `cd ../bin/Debug_x64/FlexEngine`
7. `./Flex`


## Pre-built binaries (linux-only)
To download prebuilt dependencies (not the full engine) follow the following steps:

1. `wget https://raw.githubusercontent.com/ajweeks/ajweeks.github.io/master/flex_binaries/linux-libs-debug.zip`
2. `unzip linux-libs-debug.zip -d FlexEngine/lib/x64/Debug/`

This is mostly provided for continuous integration reasons, and so is less well tested by humans.

## Troubleshooting

---

If some libraries can't be found but are installed (e.g., "cannot find -lopenal", but `/usr/lib64/libopenal.so.1` exists), create a symlink as follows:

`ln -s /usr/lib64/libopenal.so.1 /usr/lib64/libopenal.so`

If you get an error while building a dependency on linux which is similar to "Syntax error near unexpected token 'elif'"", a script likely has incorrect line endings. For example, freetype/autogen.sh may have to be converted using `dos2unix autogen.sh autogen.sh`

---

If you get the following error on startup:

`INTEL-MESA: warning: Haswell Vulkan support is incomplete`

Add the following line to `~/.profile`:

`export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json`

Then run `source ~/.profile`


## Debugging with RenderDoc

Flex Engine supports triggering RenderDoc captures directly. You can of course capture RenderDoc frames normally as well, but the integration allows you to capture with a single keypress. Follow these steps to enable the integration:
1. Set the `COMPILE_RENDERDOC_API` define to `1` in stdafx.hpp
2. Compile a recent version RenderDoc locally
3. Launch your local build of RenderDoc and register it as the system-wide Vulkan handler
4. Launch Flex Engine and specify the absolute path to `renderdoc.dll` via Edit > RenderDoc DLL path (e.g., `C:/renderdoc/x64/Development/renderdoc.dll`)
5. Relaunch the engine and the connection should be made
  a. If successful, you will see a line logged such as: "### RenderDoc API v1.4.2 connected, F9 to capture ###"
6. Press F9 to trigger a capture. If successful, it will automatically load in RenderDoc. Captures are temporarily saved in `FlexEngine/saved/RenderDocCaptures/`, you need to manually save them to keep them persistently though.
