
--
-- NOTES:
-- - DLLs should be built using the
--     - Multi-threaded Debug DLL flag (/MDd) in debug
--     - Multi-threaded DLL flag (/MD)        in release
--

PROJECT_DIR = path.getabsolute("..")
SOURCE_DIR = path.join(PROJECT_DIR, "FlexEngine/")
DEPENDENCIES_DIR = path.join(SOURCE_DIR, "dependencies/")

solution "Flex"
	configurations {
		"Debug",
		"Development",
		"Shipping",
		"Shipping_WithSymbols"
	}

	platforms { "x32", "x64" }

	language "C++"

	location "../build/"
	objdir "../build/"
	windowstargetplatformversion "10.0.17763.0"

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


function configName(config)
	local cfgStr = ""
	if (string.startswith(config, "Debug"))
		then cfgStr = "Debug"
		else cfgStr = "Release"
	end
	return cfgStr
end

function platformLibraries(config)
	local cfgs = configurations()
	local pfms = platforms()
	for p = 1, #pfms do
		local platformStr = pfms[p]
		for i = 1, #cfgs do
			local cfgStr = configName(cfgs[i])
			local subdir = path.join(platformStr, cfgStr)
			configuration { config, pfms[p], cfgs[i] }
			libdirs { path.join(SOURCE_DIR, path.join("lib/", subdir)) }
		end
	end
	configuration {}
end


-- copy files that are specific for the platform being built for
function windowsPlatformPostBuild()
	local cfgs = configurations()
	local p = platforms()
	for i = 1, #cfgs do
		for j = 1, #p do
			configuration { "vs*", cfgs[i], p[j] }
				--copy dlls and resources after build
				local cfgStr = configName(cfgs[i])
				postbuildcommands {
					-- TODO: Copy into x32 & x64 build dirs
					"copy \"$(SolutionDir)..\\FlexEngine\\lib\\" .. p[j] .. "\\" .. cfgStr .. "\\openal32.dll\" \"$(OutDir)\\\""
				}
		end
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
configuration "x32"
	defines "FLEX_32"
configuration "x64"
	defines "FLEX_64"
configuration {}

configuration {}
	flags { "NoIncrementalLink", "NoEditAndContinue", "NoJMC" }
	includedirs { path.join(SOURCE_DIR, "include") }

	-- Files to include that shouldn't get warnings reported on
	systemincludedirs {
		path.join(SOURCE_DIR, "include/ThirdParty"),
		path.join(DEPENDENCIES_DIR, "glfw/include"),
		path.join(DEPENDENCIES_DIR, "glm"),
		path.join(DEPENDENCIES_DIR, "stb"),
		path.join(DEPENDENCIES_DIR, "imgui"),
		path.join(DEPENDENCIES_DIR, "vulkan/include"),
		path.join(DEPENDENCIES_DIR, "bullet/src"),
		path.join(DEPENDENCIES_DIR, "openAL/include"),
		path.join(DEPENDENCIES_DIR, "freetype/include"),
		path.join(DEPENDENCIES_DIR, "shaderc/libshaderc/include"),
		path.join(DEPENDENCIES_DIR, "shaderc/libshaderc_spvc/include"),
		path.join(DEPENDENCIES_DIR, "shaderc/libshaderc_util/include"),
		DEPENDENCIES_DIR,
	}

	debugdir "$(OutDir)"
configuration "vs*"
	defines { "PLATFORM_Win", "_WINDOWS" }
	linkoptions { "/ignore:4221", "/NODEFAULTLIB:LIBCMTD" }
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
	defines { "_CONSOLE" }
	location "../build"
	outputDirectories("FlexEngine")

	iif(os.is("windows"), platformLibraries("vs*"), platformLibraries("linux*"))

	configuration "vs*"
		flags { "Winmain" }
		links { "opengl32" }
		buildoptions_cpp {
			"/w14263", -- 'function' : member function does not override any base class virtual member function
			"/w14264", -- 'virtual_function' : no override available for virtual member function from base 'class'; function is hidden
			"/w14265", -- 'class' : class has virtual functions, but destructor is not virtual
			"/w14266", -- 'function' : no override available for virtual member function from base 'type'; function is hidden
			"/w15031", -- #pragma warning(pop): likely mismatch, popping warning state pushed in different file
			"/w15032", -- detected #pragma warning(push) with no corresponding #pragma warning(pop)
		}
	configuration "linux*"
		linkoptions {
			-- lpthread for shaderc?
			"-pthread", -- For pthread_create
			"-ldl", -- For dlopen, etc.
		}
		buildoptions {
			"-Wfatal-errors",
			"-march=haswell"
		}
		buildoptions_cpp {
			-- Ignored warnings:
			"-Wno-reorder", "-Wno-unused-parameter", "-Wno-switch"
		}
		buildoptions_c {
			-- no-reorder isn't valid in c
		}
	configuration {}

	windowsPlatformPostBuild()

--Linked libraries
	-- Windows
		-- Common
		configuration "vs*"
			links {
				"opengl32", "glfw3", "OpenAL32",
				"Rpcrt4" -- For UuidCreate
			}
		-- Debug-only
		configuration { "vs*", "Debug" }
			links { "BulletCollision_Debug", "BulletDynamics_Debug", "LinearMath_Debug", "freetype", "shaderc_combined" }
		configuration { "vs*", "Development" }
			links { "BulletCollision", "BulletDynamics", "LinearMath", "freetype" }
		configuration { "vs*", "Shipping" }
			links { "BulletCollision", "BulletDynamics", "LinearMath", "freetype" }
		configuration { "vs*", "Shipping_WithSymbols" }
			links { "BulletCollision", "BulletDynamics", "LinearMath", "freetype" }
	-- linux
		configuration "linux*"
			links { "glfw3", "openal", "BulletDynamics", "BulletCollision", "LinearMath", "freetype", "X11", "png", "z", "shaderc_combined" }
configuration {}

--Source files
files {
	path.join(SOURCE_DIR, "include/**.h"),
	path.join(SOURCE_DIR, "include/**.hpp"),
	path.join(SOURCE_DIR, "src/**.cpp"),
	path.join(SOURCE_DIR, "src/**.c"),
	path.join(DEPENDENCIES_DIR, "imgui/**.h"),
	path.join(DEPENDENCIES_DIR, "imgui/**.cpp"),
	path.join(DEPENDENCIES_DIR, "volk/volk.h"),
	path.join(PROJECT_DIR, "AdditionalFiles/**.natvis")
}

--Exclude the following files from the build, but keep in the project
removefiles {
}

-- Don't use pre-compiled header for the following files
nopch {
	path.join(DEPENDENCIES_DIR, "imgui/**.cpp"),
}
