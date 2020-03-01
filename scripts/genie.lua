
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
		"x32",
		--"x64",
	}

	language "C++"

	location "../build/"
	objdir "../build/"
	windowstargetplatformversion "10.0.17763.0"


PROJECT_DIR = path.getabsolute("..")
SOURCE_DIR = path.join(PROJECT_DIR, "FlexEngine/")
DEPENDENCIES_DIR = path.join(SOURCE_DIR, "dependencies/")


-- Put intermediate files under build/Intermediate/config_platform/project
-- Put binaries under bin/config/project/platform --TODO: Really? confirm
-- TODO: Remove project cause we only have one
function outputDirectories(_project)
	local cfgs = configurations()
	local p = platforms()
	for i = 1, #cfgs do
		for j = 1, #p do
			configuration { cfgs[i], p[j] }
				targetdir("../bin/" .. cfgs[i] .. "_" .. p[j] .. "/" .. _project)
				objdir("../build/Intermediate/" .. cfgs[i]  .. "/" .. _project)		--seems like the platform will automatically be added
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

configuration {}
	flags { "NoIncrementalLink", "NoEditAndContinue" }
	includedirs {
		path.join(SOURCE_DIR, "include"),
	}

	-- Files to include that shouldn't get warnings reported on
	systemincludedirs {
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
configuration "vs*"
	defines { "PLATFORM_Win" }
	linkoptions { "/ignore:4221" }
configuration { "vs*", "x32" }
	flags { "EnableSSE2" }
	defines { "WIN32" }
configuration { "x32" }
	defines { "PLATFORM_x32" }
configuration "linux*"
	defines { "linux", "__linux", "__linux__" }
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

	configuration "linux*"
		linkoptions {
			"-pthread", -- For pthread_create
			"-ldl", -- For dlopen, etc.
			"-L../../FlexEngine/lib/Debug/",
		}
		buildoptions {
			"-Wfatal-errors"
		}
		buildoptions_cpp {
			-- Ignored warnings:
			"-Wno-reorder", "-Wno-unused-parameter"
		}
		buildoptions_c {
			-- no-reorder isn't valid in c
		}

	platformLibraries()
	staticPlatformLibraries()
	windowsPlatformPostBuild()

--Linked libraries
	-- Windows
		-- Common
		configuration "vs*"
			links { "opengl32", "glfw3", "vulkan-1", "OpenAL32" }
		-- Debug-only
		configuration { "vs*", "Debug" }
			links { "BulletCollision_Debug", "BulletDynamics_Debug", "LinearMath_Debug", "freetyped" }
		configuration { "vs*", "Development" }
			links { "BulletCollision", "BulletDynamics", "LinearMath", "freetype" }
		configuration { "vs*", "Shipping" }
			links { "BulletCollision", "BulletDynamics", "LinearMath", "freetype" }
		configuration { "vs*", "Shipping_WithSymbols" }
			links { "BulletCollision", "BulletDynamics", "LinearMath", "freetype" }
	-- linux
		configuration "linux*"
			links { "glfw3", "openal", "BulletDynamics", "BulletCollision", "LinearMath", "freetype" } -- freetyped  "Bullet3Dynamics", "Bullet3Collision", 
configuration {}

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
}

-- Don't use pre-compiled header for the following files
nopch {
	path.join(DEPENDENCIES_DIR, "imgui/**.cpp"),
	path.join(DEPENDENCIES_DIR, "glad/src/glad.c")
}

pchheader "stdafx.hpp"
pchsource "stdafx.cpp"
