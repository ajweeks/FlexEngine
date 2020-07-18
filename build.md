![](FlexEngine/screenshots/flex_engine_banner_3.png)

[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

# Building Flex

If you want to build Flex Engine on your own system, follow these steps. You an always download the latest release binaries [here](https://github.com/ajweeks/flexengine/releases) if that's what you're after.

### Windows
#### Requirements:
- [GENie](https://github.com/bkaradzic/GENie)
- cmake 3.13+
- Python 3

#### Steps
1. `cd scripts`
2. `python3 build_dependencies.py windows vs2019`
3. Open `build/Flex.sln`
4. Change configuration to x64
5. Build and run!

NOTE: If GENie isn't on your path, you will need to run `genie --file=scripts/genie.lua {build_system}` manually after running the build script in step 2, specifying whichever build system you want in place of `{build_system}`, such as `vs2019`.

### Linux (tested only on Ubuntu 18.04, enter at your own risk)
#### Requirements:
- [GENie](https://github.com/bkaradzic/GENie) ([binary](https://github.com/bkaradzic/bx/raw/master/tools/bin/linux/genie))
- Python 3
- cmake 3.13+

#### Steps
1. Run the following commands to install prerequisites:
  - `sudo apt update`
  - `sudo apt-get install g++-multilib libopenal-dev python3-dev xserver-xorg-dev libxcursor-dev libxi-dev vulkan-sdk automake libtool autoconf libbz2-dev`
  - `wget -qO - http://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -`
  - `sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.2.131-bionic.list http://packages.lunarg.com/vulkan/1.2.131/lunarg-vulkan-1.2.131-bionic.list` (substitute in any newer vulkan version)
2. `cd scripts`
3. `python3 build_dependencies.py linux gmake`
5. `ninja -C debug64`
6. `cd ../bin/Debug_x64/FlexEngine`
7. `./Flex`
