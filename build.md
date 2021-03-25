![](FlexEngine/screenshots/flex_engine_banner_3.png)

[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

# Building Flex

If you want to build Flex Engine on your own system, follow these steps. You an always download the latest release binaries [here](https://github.com/ajweeks/flexengine/releases) if that's what you're after.

## Windows
#### Requirements:
- [GENie](https://github.com/bkaradzic/GENie)
- cmake 3.13+
- Python 3

#### Steps
1. `cd scripts`
2. `python build_dependencies.py windows vs2019`
3. Open `build/Flex.sln`
4. Change configuration to x64
5. Build and run!

NOTE: If GENie isn't on your path, you will need to run `genie --file=scripts/genie.lua {build_system}` manually after running the build script in step 2, specifying whichever build system you want in place of `{build_system}`, such as `vs2019`.

## Linux
#### Requirements:
- [GENie](https://github.com/bkaradzic/GENie)
- Python 3
- cmake 3.13+

### Ubuntu 18.04
#### Steps
1. Run the following commands to install prerequisites:
  - `sudo apt update`
  - `sudo apt-get install g++-multilib libopenal-dev python3-dev xserver-xorg-dev libxcursor-dev libxi-dev libxrandr-dev libxinerama-dev automake libtool autoconf libbz2-dev uuid-dev`
  - `wget -qO - http://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -`
  - `sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.2.131-bionic.list http://packages.lunarg.com/vulkan/1.2.131/lunarg-vulkan-1.2.131-bionic.list` (substitute in any newer vulkan version)
2. `cd scripts`
3. `python3 build_dependencies.py linux gmake`
4. `make`
5. `cd ../bin/Debug_x64/FlexEngine`
6. `./Flex`
### Linux Ubuntu 18.04
#### Requirements:
- [GENie](https://github.com/bkaradzic/GENie)
- Python 3
- cmake 3.13+

### Solus 4.2
#### Steps
1. Run the following commands to install prerequisites:
  - `sudo eopkg upgrade`
  - `sudo eopkg install gcc llvm-clang glibc-devel libx11-devel libxcursor-devel vulkan automake libtool autoconf make libxrandr-devel libxinerama-devel libxi-devel`
  - `sudo eopkg install -c system.devel uuid-dev`
  - Install latest vulkan sdk by following steps here: https://vulkan.lunarg.com/sdk/home
2. `cd scripts`
3. `git config --global init.defaultBranchName main`
4. `python3 build_dependencies.py linux gmake`
5. `make`
6. `cd ../bin/Debug_x64/FlexEngine`
7. `./Flex`

### Fedora
#### Steps
1. Run the following commands to install prerequisites:
  - `sudo dnf update`
  - `sudo dnf install gcc glibc-devel libx11-devel libXcursor-devel vulkan automake libtool autoconf make libXrandr-devel libXinerama-devel libXi-devel openal-soft libpng zlib uuid-dev`
  - Install latest vulkan sdk by following steps here: https://vulkan.lunarg.com/sdk/home
2. `cd scripts`
3. `git config --global init.defaultBranchName main`
4. `python3 build_dependencies.py linux gmake`
5. `make`
6. `cd ../bin/Debug_x64/FlexEngine`
7. `./Flex`


**Troubleshooting:**

If some libraries can't be found but are installed (eg. "cannot find -lopenal", but `/usr/lib64/libopenal.so.1` exists), create a symlink as follows:

`ln -s /usr/lib64/libopenal.so.1 /usr/lib64/libopenal.so`

If you get an error while building a dependency on linux which is similar to "Syntax error near unexpected token 'elif'"", a script likely has incorrect line endings. For example, freetype/autogen.sh may have to be converted using `dos2unix autogen.sh autogen.sh`
