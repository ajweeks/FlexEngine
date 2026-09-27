#!/usr/bin/env python3

import argparse
import os
import re
import shutil
import subprocess
import sys
import time

# Show progress immediately, even when output is redirected to a file
sys.stdout.reconfigure(line_buffering=True)

shaderc_tag = 'v2026.4'

# genie target -> (CMake generator, vswhere version range)
vs_targets = {
	'vs2019': ('Visual Studio 16 2019', '[16.0,17.0)'),
	'vs2022': ('Visual Studio 17 2022', '[17.0,18.0)'),
	'vs2026': ('Visual Studio 18 2026', '[18.0,19.0)'),
}

supported_configs = ['Debug', 'Sanitize', 'Profile', 'Release', 'All']
supported_platforms = ['windows', 'linux']

# Configs which define DEBUG, and so compile in the runtime shader compiler (see COMPILE_SHADER_COMPILER in stdafx.hpp)
shader_compiler_configs = ['Debug', 'Sanitize']

min_cmake_version = (3, 15)

program_files_x86 = os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')


def version_tuple(version_str):
	return tuple(int(x) for x in re.findall(r'\d+', version_str))


# Returns (installation path, cmake generator instance)
def find_vs_install(version_range):
	vswhere_path = os.path.join(program_files_x86, 'Microsoft Visual Studio/Installer/vswhere.exe')
	if not os.path.exists(vswhere_path):
		return (None, None)

	def vswhere(property, extra_args = []):
		found = subprocess.check_output([vswhere_path, '-version', version_range, '-latest', '-products', '*',
			'-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', property] + extra_args).decode().strip()
		return found.splitlines()[0].strip() if found else None

	path = vswhere('installationPath')
	if path:
		return (path, path)

	# Instances pending a reboot after installing/updating are considered incomplete, but are generally still usable.
	# cmake only accepts these when told their version explicitly.
	path = vswhere('installationPath', ['-all'])
	if not path:
		return (None, None)
	print('Warning: Visual Studio installation is incomplete (a reboot may be pending, or the install may need repairing)')
	return (path, path + ',version=' + vswhere('installationVersion', ['-all']))


def find_windows_sdk():
	sdk_dir = os.path.join(program_files_x86, 'Windows Kits/10')
	include_dir = os.path.join(sdk_dir, 'Include')
	if not os.path.exists(include_dir):
		return None
	# Partially installed SDKs (e.g. missing x64 libs) fail cmake's compiler detection with a misleading error
	required_files = ['Include/{0}/um/Windows.h', 'Lib/{0}/um/x64/kernel32.lib', 'Lib/{0}/ucrt/x64/ucrt.lib', 'Lib/{0}/ucrt/x64/ucrtd.lib']
	def is_complete(version):
		missing = [f.format(version) for f in required_files if not os.path.exists(os.path.join(sdk_dir, f.format(version)))]
		# Only warn about SDKs with headers installed, older versions often leave behind just the UCRT
		if missing and os.path.exists(os.path.join(include_dir, version, 'um/Windows.h')):
			print('Skipping incomplete Windows SDK ' + version + ' (missing ' + ', '.join(missing) + ')')
		return not missing
	sdks = [d for d in os.listdir(include_dir) if re.match(r'^10\.0\.\d+\.\d+$', d) and is_complete(d)]
	return max(sdks, key=version_tuple) if sdks else None


def get_cmake_version(path):
	try:
		output = subprocess.check_output([path, '--version']).decode()
	except (OSError, subprocess.CalledProcessError):
		return None
	match = re.search(r'cmake version (\d+)\.(\d+)\.(\d+)', output)
	return tuple(int(x) for x in match.groups()) if match else None


def find_cmake(generator, vs_install_path):
	# Prefer cmake on the PATH, falling back to the one bundled with Visual Studio
	candidates = [shutil.which('cmake')]
	if vs_install_path:
		candidates.append(os.path.join(vs_install_path, 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'))
	for candidate in candidates:
		if not candidate or not os.path.exists(candidate):
			continue
		version = get_cmake_version(candidate)
		if not version or version < min_cmake_version:
			continue
		if generator and generator not in subprocess.check_output([candidate, '--help']).decode():
			continue
		return (candidate, version)
	return (None, None)


parser = argparse.ArgumentParser(description='Builds Flex Engine dependencies and generates project files',
	epilog='e.g: "python ' + sys.argv[0] + ' windows vs2026 Debug"')
parser.add_argument('platform', choices=supported_platforms)
parser.add_argument('target', help='genie target (' + ', '.join(vs_targets) + ' on windows, gmake or ninja on linux)')
parser.add_argument('config', choices=supported_configs)
parser.add_argument('build_extras', nargs='?', choices=['build_extras'], help='Also build bullet demos & extras')
parser.add_argument('--windows-sdk', help='Windows SDK version to target (defaults to the latest installed)')
args = parser.parse_args()

platform = args.platform
genie_target = args.target
in_config = args.config
build_extras = args.build_extras is not None

if platform == 'windows':
	if genie_target not in vs_targets:
		print('Invalid target specified. Must be one of: ' + ', '.join(vs_targets))
		exit(1)

	(cmake_generator, vs_version_range) = vs_targets[genie_target]
	(vs_install_path, vs_generator_instance) = find_vs_install(vs_version_range)
	if not vs_install_path:
		print('Failed to find a Visual Studio installation with C++ tools for ' + genie_target)
		exit(1)
	print('Using Visual Studio at ' + vs_install_path)

	windows_sdk = args.windows_sdk or find_windows_sdk()
	if not windows_sdk:
		print('Failed to find a Windows SDK, please install one through the Visual Studio installer')
		exit(1)
	print('Using Windows SDK ' + windows_sdk)
else:
	cmake_generator = 'Ninja' if shutil.which('ninja') else 'Unix Makefiles'
	vs_install_path = None
	vs_generator_instance = None
	windows_sdk = None

(cmake_path, cmake_version) = find_cmake(cmake_generator if platform == 'windows' else None, vs_install_path)
if not cmake_path:
	print('Failed to find cmake {0}.{1}+'.format(*min_cmake_version) + (' supporting "' + cmake_generator + '"' if platform == 'windows' else '') + ', please update then retry building.')
	exit(1)
print('Using cmake {0}.{1}.{2} at '.format(*cmake_version) + cmake_path)

git_path = shutil.which('git') or 'C:/Program Files/Git/bin/git.exe'
# Prefer a genie binary next to this script, falling back to the PATH
genie_name = 'genie.exe' if platform == 'windows' else 'genie'
genie_path = os.path.abspath(genie_name) if os.path.exists(genie_name) else shutil.which(genie_name)
if not genie_path:
	print('Failed to find genie, please place it in the scripts directory or on your PATH (https://github.com/bkaradzic/GENie)')
	exit(1)
python_path = sys.executable

cmake_common_args = ['-G', cmake_generator, '-Wno-dev']
if platform == 'windows':
	cmake_common_args += ['-A', 'x64', '-DCMAKE_GENERATOR_INSTANCE=' + vs_generator_instance, '-DCMAKE_SYSTEM_VERSION=' + windows_sdk]
if cmake_version >= (4, 0):
	# Most dependencies declare a cmake_minimum_required below 3.5, which cmake 4 no longer accepts.
	# Set through the environment so nested cmake invocations (e.g. OpenAL's native-tools) pick it up too.
	os.environ['CMAKE_POLICY_VERSION_MINIMUM'] = '3.5'


def read_cmake_cache_value(build_path, key):
	cache_path = os.path.join(build_path, 'CMakeCache.txt')
	if not os.path.exists(cache_path):
		return None
	with open(cache_path, 'r', errors='ignore') as f:
		for line in f:
			if line.startswith(key + ':'):
				return line.split('=', 1)[1].strip()
	return None


# Windows builds use a single multi-config build directory, linux builds use one per config.
# Keeping these separate allows building both platforms from the same checkout (e.g. under WSL).
# Windows Sanitize builds get their own directory since they're compiled with different flags (see sanitize_cmake_args).
def get_build_path(dependency_path, config, name = 'build'):
	root = dependency_path + name + '/'
	# Clean up the old layout, which configured directly in the root build directory
	if os.path.exists(root + 'CMakeCache.txt'):
		print('Removing outdated build directory ' + root)
		shutil.rmtree(root)
	if platform == 'windows':
		return root + ('windows-sanitize/' if config == 'Sanitize' else 'windows/')
	return root + 'linux-' + get_external_config(config).lower() + '/'


def get_external_config(config):
	return 'Debug' if (config == 'Debug' or config == 'Sanitize') else 'Release'


# Extra cmake args for static libraries linked into Flex
def get_config_cmake_args(config):
	if platform != 'windows' or config != 'Sanitize':
		return []
	# Static libs linked into an ASan build must also be built with ASan, otherwise MSVC's STL container annotations
	# mismatch (LNK2038 'annotate_vector'/'annotate_string'). /RTC1 (in cmake's default debug flags) is incompatible with ASan.
	# Debug info is embedded in the objects (/Z7) so no separate PDBs are needed when linking Flex (LNK4099).
	debug_flags = '/MDd /Z7 /Ob0 /Od /fsanitize=address'
	return [
		'-DCMAKE_C_FLAGS_DEBUG=' + debug_flags,
		'-DCMAKE_CXX_FLAGS_DEBUG=' + debug_flags,
		'-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded']


def run_cmake(source, build, arguments = []):
	# A build directory can't be reused with a different generator, platform, or Visual Studio instance
	cached_generator = read_cmake_cache_value(build, 'CMAKE_GENERATOR')
	if cached_generator is not None:
		stale = cached_generator != cmake_generator
		if platform == 'windows':
			cached_platform = read_cmake_cache_value(build, 'CMAKE_GENERATOR_PLATFORM') or ''
			cached_instance = (read_cmake_cache_value(build, 'CMAKE_GENERATOR_INSTANCE') or '').split(',')[0]
			stale = stale or cached_platform != 'x64' or os.path.normcase(os.path.normpath(cached_instance)) != os.path.normcase(os.path.normpath(vs_install_path))
		if stale:
			print('Removing stale build directory ' + build + ' (configured with ' + cached_generator + ')')
			shutil.rmtree(build)

	os.makedirs(build, exist_ok=True)
	subprocess.check_call([cmake_path, '-S', source, '-B', build] + cmake_common_args + arguments, stderr=subprocess.STDOUT)


def run_cmake_build(path, config, targets = None):
	args = [cmake_path, '--build', path, '--config', config, '--parallel', str(os.cpu_count() or 4)]
	if targets:
		args += ['--target'] + targets
	if platform == 'windows':
		# Compile files within each project in parallel too, not just separate projects
		args += ['--', '/p:UseMultiToolTask=true', '/p:EnforceProcessCountAcrossBuilds=true', '/nologo', '/verbosity:minimal']
	subprocess.check_call(args, stderr=subprocess.STDOUT)


def run_git(arguments, cwd = None):
	subprocess.check_call([git_path] + arguments, stderr=subprocess.STDOUT, cwd=cwd)


def copy_first_existing(candidates, target):
	for candidate in candidates:
		if os.path.exists(candidate):
			shutil.copy(candidate, target)
			return
	raise FileNotFoundError('None of the following files exist: ' + ', '.join(candidates))


def print_header(title):
	print("\n------------------------------------------\n\n" + title + "\n\n------------------------------------------\n")


start_time = time.perf_counter()


def build_project(config):
	project_root = '../FlexEngine/'
	libs_target = project_root + 'lib/x64/' + config + '/'
	external_config = get_external_config(config)
	build_type_arg = '-DCMAKE_BUILD_TYPE=' + external_config
	config_args = get_config_cmake_args(config)

	print("\nBuilding " + config + " config (external: " + external_config + (", with ASan" if config_args else "") + ")\n\n")

	os.makedirs(libs_target, exist_ok=True)

	print_header('Building GLFW...')

	# GLFW
	glfw_path = project_root + 'dependencies/glfw/'
	glfw_build_path = get_build_path(glfw_path, config)
	run_cmake(glfw_path, glfw_build_path, [
		'-DGLFW_BUILD_EXAMPLES=OFF',
		'-DGLFW_BUILD_TESTS=OFF',
		'-DGLFW_BUILD_DOCS=OFF',
		'-DGLFW_INSTALL=OFF',
		build_type_arg] + config_args)
	run_cmake_build(glfw_build_path, external_config, ['glfw'])

	if platform == 'windows':
		shutil.copy(glfw_build_path + 'src/' + external_config + '/glfw3.lib', libs_target)
	else:
		shutil.copy(glfw_build_path + 'src/libglfw3.a', libs_target)

	# OpenAL
	# (On linux we just use the prebuilt libopenal-dev package)
	if platform == 'windows':
		print_header('Building OpenAL...')
		openAL_path = project_root + 'dependencies/openAL/'
		# OpenAL is a DLL, so doesn't need to match Flex's ASan setting and can share the Debug build
		openAL_build_path = get_build_path(openAL_path, external_config)
		run_cmake(openAL_path, openAL_build_path, [
			'-DALSOFT_UTILS=OFF',
			'-DALSOFT_NO_CONFIG_UTIL=ON',
			'-DALSOFT_EXAMPLES=OFF',
			'-DALSOFT_TESTS=OFF',
			'-DALSOFT_INSTALL=OFF',
			'-DALSOFT_CONFIG=OFF',
			'-DALSOFT_HRTF_DEFS=OFF',
			'-DALSOFT_AMBDEC_PRESETS=OFF',
			build_type_arg])
		run_cmake_build(openAL_build_path, external_config, ['OpenAL'])
		openal_lib_path = openAL_build_path + external_config + '/'
		shutil.copy(openal_lib_path + 'OpenAL32.dll', libs_target)
		shutil.copy(openal_lib_path + 'OpenAL32.lib', libs_target)

	print_header('Building Bullet...')

	# Bullet (only the collision, dynamics & math libraries are used)
	bullet_path = project_root + 'dependencies/bullet/'
	bullet_build_path = get_build_path(bullet_path, config)
	run_cmake(bullet_path, bullet_build_path, [
		'-DUSE_MSVC_RUNTIME_LIBRARY_DLL=ON',
		'-DUSE_GRAPHICAL_BENCHMARK=OFF',
		'-DBUILD_UNIT_TESTS=OFF',
		'-DBUILD_CPU_DEMOS=OFF',
		'-DBUILD_BULLET2_DEMOS=OFF',
		'-DBUILD_OPENGL3_DEMOS=OFF',
		'-DBUILD_BULLET3=OFF',
		'-DBUILD_PYBULLET=OFF',
		'-DBUILD_ENET=OFF',
		'-DBUILD_CLSOCKET=OFF',
		'-DBUILD_EXTRAS=OFF',
		'-DINSTALL_LIBS=OFF',
		build_type_arg] + config_args)
	bullet_libs = ['BulletCollision', 'BulletDynamics', 'LinearMath']
	run_cmake_build(bullet_build_path, external_config, bullet_libs)
	for lib in bullet_libs:
		if platform == 'windows':
			suffix = '_Debug' if external_config == 'Debug' else ''
			shutil.copy(bullet_build_path + 'lib/' + external_config + '/' + lib + suffix + '.lib', libs_target)
		else:
			shutil.copy(bullet_build_path + 'src/' + lib + '/lib' + lib + '.a', libs_target)

	if build_extras:
		print_header('Building Bullet Full...')

		# Bullet Full (Build demos & extras, not for use in Flex, but for inspecting separately)
		bullet_full_build_path = get_build_path(bullet_path, external_config, 'build_full')
		run_cmake(bullet_path, bullet_full_build_path, ['-DUSE_MSVC_RUNTIME_LIBRARY_DLL=ON', '-DBUILD_UNIT_TESTS=OFF', '-DBUILD_CPU_DEMOS=ON', '-DBUILD_BULLET2_DEMOS=ON', '-DBUILD_EXTRAS=ON', build_type_arg])
		run_cmake_build(bullet_full_build_path, external_config)

	print_header('Building FreeType...')

	# FreeType (built as a static library, without any optional dependencies)
	free_type_path = project_root + 'dependencies/freetype/'
	free_type_build_path = get_build_path(free_type_path, config)
	run_cmake(free_type_path, free_type_build_path, [
		'-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=TRUE',
		'-DCMAKE_DISABLE_FIND_PACKAGE_BZip2=TRUE',
		'-DCMAKE_DISABLE_FIND_PACKAGE_PNG=TRUE',
		'-DCMAKE_DISABLE_FIND_PACKAGE_HarfBuzz=TRUE',
		'-DCMAKE_DISABLE_FIND_PACKAGE_BrotliDec=TRUE',
		build_type_arg] + config_args)
	run_cmake_build(free_type_build_path, external_config, ['freetype'])
	if platform == 'windows':
		# Debug builds are given a 'd' postfix, Flex links against 'freetype' in all configs
		free_type_lib_path = free_type_build_path + external_config + '/'
		copy_first_existing([free_type_lib_path + 'freetyped.lib', free_type_lib_path + 'freetype.lib'], libs_target + 'freetype.lib')
	else:
		copy_first_existing([free_type_build_path + 'libfreetyped.a', free_type_build_path + 'libfreetype.a'], libs_target + 'libfreetype.a')

	if config not in shader_compiler_configs:
		print('\nSkipping Shaderc, the shader compiler is only compiled in to ' + ', '.join(shader_compiler_configs) + ' builds\n')
		return

	print_header('Building Shaderc...')

	# Shaderc
	shader_c_path = project_root + 'dependencies/shaderc/'

	# Delete existing directory if on incorrect commit
	if os.path.exists(shader_c_path):
		curr_shader_c_commit = subprocess.check_output([git_path, 'describe', '--tags', '--always'], cwd=shader_c_path).decode().strip()
		matched = curr_shader_c_commit == shaderc_tag
		print('On commit ' + curr_shader_c_commit + ', required commit: ' + shaderc_tag +
			(' (matched, skipping clone of repo)' if matched else ' (not matched, re-cloning repo)'))
		if not matched:
			# Remove existing shaderc directory (shutil.rmtree fails here with permission errors on windows, use the big guns instead)
			if platform == 'windows':
				os.system('rmdir /S /Q "{}"'.format(os.path.normpath(shader_c_path)))
			else:
				os.system('rm -rdf "{}"'.format(shader_c_path))

	if not os.path.exists(shader_c_path):
		run_git(['clone', 'https://github.com/google/shaderc', shader_c_path, '--branch', shaderc_tag, '--depth=1'])

	shader_c_build_path = get_build_path(shader_c_path, config)

	os.environ['GIT_EXECUTABLE'] = git_path
	subprocess.check_call([python_path, shader_c_path + 'utils/git-sync-deps'], stderr=subprocess.STDOUT)

	run_cmake(shader_c_path, shader_c_build_path, [
		'-DSHADERC_SKIP_TESTS=ON',
		'-DSHADERC_SKIP_EXAMPLES=ON',
		'-DSHADERC_SKIP_EXECUTABLES=ON',
		# Install rules must stay enabled: with SHADERC_SKIP_INSTALL=ON, shaderc sets GLSLANG_ENABLE_INSTALL to
		# an (always truthy) generator expression, so glslang exports targets depending on unexported SPIRV-Tools
		'-DSHADERC_SKIP_INSTALL=OFF',
		'-DSHADERC_SKIP_COPYRIGHT_CHECK=ON',
		'-DSHADERC_ENABLE_SHARED_CRT=ON',
		'-DSHADERC_ENABLE_WERROR_COMPILE=OFF',
		'-DENABLE_GLSLANG_BINARIES=OFF',
		'-DSPIRV_SKIP_EXECUTABLES=ON',
		'-DSPIRV_SKIP_TESTS=ON',
		'-DSPIRV_WERROR=OFF',
		'-DSPIRV_HEADERS_SKIP_EXAMPLES=ON',
		build_type_arg] + config_args)
	run_cmake_build(shader_c_build_path, external_config, ['shaderc_combined'])

	if platform == 'windows':
		shutil.copy(shader_c_build_path + 'libshaderc/' + external_config + '/shaderc_combined.lib', libs_target)
	else:
		shutil.copy(shader_c_build_path + 'libshaderc/libshaderc_combined.a', libs_target)


print("\nBuilding Flex Engine..." + ("(with extras)" if build_extras else "") + "\n")

if in_config == 'All':
	for c in supported_configs[:-1]:
		build_project(c)
else:
	build_project(in_config)


print_header('Building genie project...')

# Project
genie_args = [genie_path, '--file=genie.lua']
if windows_sdk:
	genie_args += ['--windows-sdk=' + windows_sdk]
subprocess.check_call(genie_args + [genie_target], stderr=subprocess.STDOUT)

end_time = time.perf_counter()
total_elapsed = end_time - start_time
print("Building all Flex dependencies took {0:0.1f} sec ({1:0.1f} min)".format(total_elapsed, total_elapsed / 60.0))
