![](FlexEngine/screenshots/flex_engine_banner_3.png)

[![MIT licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

# Building Flex

If you want to build Flex Engine on your own system, follow these steps. You an always download the latest release binaries [here](https://github.com/ajweeks/flexengine/releases) if that's what you're after.

First ensure you've pulled all the dependencies, either passing `--recurse-submodules` when cloning, or using `git submodule update --init --recursive` after the fact.

## Windows
#### Requirements:
- Python 3
- Visual Studio 2019, 2022, or 2026 with the "Desktop development with C++" workload, which includes:
  - MSVC x64/x86 build tools
  - A Windows 10 or 11 SDK (the latest installed is used)
  - cmake (used if no newer cmake is on your PATH)
- cmake 3.15+ (4.2+ for VS2026), only needed if the version bundled with Visual Studio is too old
- Optional: the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home), to enable validation layers in Debug builds

#### Steps
1. `cd scripts`
2. `python build_dependencies.py windows vs2026 Debug` (or `vs2022`/`vs2019`, and `Debug`/`Sanitize`/`Profile`/`Release`/`All`)
    - Pass e.g. `--windows-sdk=10.0.26100.0` to target a specific Windows SDK
    - Re-running is incremental, only changed dependencies are rebuilt
3. Open `build/Flex.sln`
4. Build and run!


## Linux
#### Requirements:
- A C++ compiler (gcc or clang), make and/or ninja
- Python 3
- cmake 3.15+
- [GENie](https://github.com/bkaradzic/GENie), either on your PATH or copied into `scripts/`. To build it: `git clone --depth 1 https://github.com/bkaradzic/GENie && make -C GENie`, which outputs `GENie/bin/linux/genie`
- Development packages for OpenAL, X11 (plus Xcursor, Xi, Xrandr, Xinerama), and libuuid
- Optional: the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home), to enable validation layers in Debug builds

#### Installing packages
- Ubuntu/Debian: `sudo apt install build-essential cmake ninja-build python3 libopenal-dev libx11-dev libxcursor-dev libxi-dev libxrandr-dev libxinerama-dev uuid-dev`
- Fedora: `sudo dnf install gcc-c++ cmake ninja-build python3 openal-soft-devel libX11-devel libXcursor-devel libXi-devel libXrandr-devel libXinerama-devel libuuid-devel`
- Solus: `sudo eopkg install -c system.devel` then `sudo eopkg install cmake ninja python3 openal-soft-devel libx11-devel libxcursor-devel libxi-devel libxrandr-devel libxinerama-devel util-linux-devel`

#### Steps
1. `cd scripts`
2. `python3 build_dependencies.py linux ninja Debug` (or `gmake`, and `Debug`/`Sanitize`/`Profile`/`Release`/`All`)
3. `ninja -C ../build/debug64` (or `make -C ../build config=debug64` for gmake)
4. `cd ../bin/Debug_x64/FlexEngine`
5. `./Flex`


## Troubleshooting

---

If some libraries can't be found but are installed (e.g., "cannot find -lopenal", but `/usr/lib64/libopenal.so.1` exists), create a symlink as follows:

`ln -s /usr/lib64/libopenal.so.1 /usr/lib64/libopenal.so`

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
