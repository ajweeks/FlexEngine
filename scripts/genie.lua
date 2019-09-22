
-- 
-- NOTES:
-- - DLLs should be built using the Multi-threaded Debug DLL flag (/MDd) in debug,
--   and the Multi-threade DLL flag (/MD) in release
-- 


solution "Flex"
	configurations {
		"Debug",
		"Development",
		"Shipping",
		"Shipping_WithSymbols"
	}

	platforms {
		"x32"
	}

	language "C++"

	location "../build/"
	objdir "../build/"
	windowstargetplatformversion "10.0.17763.0"


PROJECT_DIR = path.getabsolute("..")
SOURCE_DIR = path.join(PROJECT_DIR, "FlexEngine/")
DEPENDENCIES_DIR = path.join(SOURCE_DIR, "dependencies/")


-- Put intermediate files under build/Intermediate/config_platform/project
-- Put binaries under bin/config/project
function outputDirectories(_project)
	local cfgs = configurations()
	local p = platforms()
	for i = 1, #cfgs do
		for j = 1, #p do
			configuration { cfgs[i], p[j] }
				targetdir("../bin/" .. cfgs[i] .. "_" .. p[j] .. "/" .. _project)
				objdir("../build/Intermediate/" .. cfgs[i]  .. "/" .. _project)
		end
	end
	configuration {}
end


function platformLibraries()
	local cfgs = configurations()
	for i = 1, #cfgs do
		local subdir = ""
		if (string.startswith(cfgs[i], "Debug"))
			then subdir = "Debug"
			else subdir = "Release"
		end
		configuration { "vs*", cfgs[i] }
			libdirs { 
				path.join(SOURCE_DIR, path.join("lib/", subdir))
			}
	end
	configuration {}
end

function staticPlatformLibraries()
	local p = platforms()
	for j = 1, #p do
		configuration { "vs*", p[j] }
			libdirs { 
				path.join(DEPENDENCIES_DIR, path.join("vulkan/lib/", p[j])),
			}
	end
	configuration {}
end


-- copy files that are specific for the platform being built for
function windowsPlatformPostBuild()
	local cfgs = configurations()

	for i = 1, #cfgs do
		--copy dlls and resources after build
		configuration { "vs*", cfgs[i] }
			postbuildcommands { 
				"copy \"$(SolutionDir)..\\FlexEngine\\lib\\openal32.dll\" " ..
				"\"$(OutDir)openal32.dll\""
			}
	end
	configuration {}
end


configuration "Debug"
	defines { "DEBUG", "SYMBOLS" }
	flags { "Symbols", "ExtraWarnings" }
configuration "Development"
	defines { "DEVELOPMENT", "SYMBOLS" }
	flags {"OptimizeSpeed", "Symbols", "ExtraWarnings" }
configuration "Shipping"
	defines { "SHIPPING" }
	flags {"OptimizeSpeed", "No64BitChecks" }
configuration "Shipping_WithSymbols"
	defines { "SHIPPING", "SYMBOLS" }
	flags {"OptimizeSpeed", "Symbols", "No64BitChecks" }
configuration {}


configuration "vs*"
	flags { "NoIncrementalLink", "NoEditAndContinue" }
	linkoptions { "/ignore:4221" }
	defines { "PLATFORM_Win" }
	includedirs { 
		path.join(SOURCE_DIR, "include"),
		path.join(DEPENDENCIES_DIR, "glad/include"),
		path.join(DEPENDENCIES_DIR, "glfw/include"), 
		path.join(DEPENDENCIES_DIR, "glm"), 
		path.join(DEPENDENCIES_DIR, "stb"), 
		path.join(DEPENDENCIES_DIR, "imgui"),
		path.join(DEPENDENCIES_DIR, "vulkan/include"),
		path.join(DEPENDENCIES_DIR, "bullet/src"),
		path.join(DEPENDENCIES_DIR, "openAL"),
		path.join(DEPENDENCIES_DIR, "freetype/include"),
		DEPENDENCIES_DIR,
	}
	debugdir "$(OutDir)"
configuration { "vs*", "x32" }
	flags { "EnableSSE2" }
	defines { "WIN32" }
configuration { "x32" }
	defines { "PLATFORM_x32" }
configuration {}


startproject "Flex"

project "Flex"
	kind "ConsoleApp"

	location "../build"

    defines { "_CONSOLE" }

	outputDirectories("FlexEngine")

	configuration "vs*"
		flags { "Winmain"}

		links { "opengl32" } 

	platformLibraries()
	staticPlatformLibraries()
	windowsPlatformPostBuild()

	--Linked libraries
    links { "opengl32", "glfw3", "vulkan-1", "OpenAL32" }


configuration { "Debug" }
	links { "BulletCollision_Debug", "BulletDynamics_Debug", "LinearMath_Debug", "freetyped" } 
configuration { "Development" }
	links { "BulletCollision", "BulletDynamics", "LinearMath", "freetype" }
configuration { "Shipping" }
	links { "BulletCollision", "BulletDynamics", "LinearMath", "freetype" } 
configuration { "Shipping_WithSymbols" }
	links { "BulletCollision", "BulletDynamics", "LinearMath", "freetype" } 
configuration {}

	--Additional includedirs
	includedirs { 
		path.join(SOURCE_DIR, "include"),
	}

	--Source files
    files {
		path.join(SOURCE_DIR, "include/**.h"), 
		path.join(SOURCE_DIR, "include/**.hpp"), 
		path.join(SOURCE_DIR, "src/**.cpp"), 
		path.join(DEPENDENCIES_DIR, "imgui/**.h"),
		path.join(DEPENDENCIES_DIR, "imgui/**.cpp"),
		path.join(DEPENDENCIES_DIR, "glad/src/glad.c"),
	}

	--Exclude the following files from the build, but keep in the project
	removefiles {
		--path.join(DEPENDENCIES_DIR, "imgui/imconfig_demo.cpp")
	}

	-- Don't use pre-compiled header for the following files
	nopch {
		path.join(DEPENDENCIES_DIR, "imgui/**.cpp"),
		path.join(DEPENDENCIES_DIR, "glad/src/glad.c")
	}

	pchheader "stdafx.hpp"
	pchsource "../FlexEngine/src/stdafx.cpp"


project "shaderc"
	kind "ConsoleApp"
	
	location "../build/shaderc"

	configuration { }

	outputDirectories("shaderc")

	includedirs { 
		path.join(DEPENDENCIES_DIR, "shaderc/libshaderc/include"),
		path.join(DEPENDENCIES_DIR, "shaderc/libshaderc_util/include"),
		path.join(DEPENDENCIES_DIR, "glslang"),
		path.join(DEPENDENCIES_DIR, "spirv-tools/include"),
	}

    files {
		path.join(DEPENDENCIES_DIR, "shaderc/libshaderc/src/*"),
	}

-- TODO: Figure out how to set stdafx.cpp to use /Yc compiler flag to generate precompiled header object

