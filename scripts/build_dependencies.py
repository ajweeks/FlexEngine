#!/usr/bin/env python3

import os
import subprocess
import shutil
import time
import sys

msbuild_path = 'C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/MSBuild/Current/Bin/MSBuild.exe'
git_path_windows = 'C:/Program Files/Git/bin/git.exe'
git_path_linux = 'git'
cmake_path_windows = 'cmake.exe'
cmake_path_linux = 'cmake'
genie_path_windows = 'genie.exe'
genie_path_linux = 'genie'
python_path_windows = 'python'
python_path_linux = 'python3'

shaderc_tag = 'v2020.2'

def run_cmake(source, build, arguments = []):
	cmakeCmd = [cmake_path, '-S', source, '-B', build]
	cmakeCmd += arguments
	subprocess.check_call(cmakeCmd, stderr=subprocess.STDOUT)

def run_cmake_build(platform, path, arguments):
	args = [cmake_path, '--build', path] + arguments
	if platform == 'linux':
		args += ['--', '-j6']
	subprocess.check_call(args, stderr=subprocess.STDOUT)

def run_msbuild(sln, arguments = []):
	cmd = [msbuild_path, sln]
	cmd += arguments
	subprocess.check_call(cmd, stderr=subprocess.STDOUT, shell=True)

def run_make(dir, dot=True):
	cmd = ['cd ' + dir + ';' + 'make ' + ('.' if dot else '')]
	subprocess.check_call(cmd, stderr=subprocess.STDOUT, shell=True)

def run_git(arguments = []):
	cmd = [git_path]
	cmd += arguments
	subprocess.check_call(cmd, stderr=subprocess.STDOUT, shell=False)


def print_usage():
	print('Usage: "python ' + sys.argv[0] + ' [platform] [target] [config] <build_extras>"\n'
			'e.g: "python ' + sys.argv[0] + ' windows vs2019 Debug"')


if len(sys.argv) < 4 or len(sys.argv) > 5:
	print_usage()
	exit(1)

platform = sys.argv[1]
genie_target = sys.argv[2]
in_config = sys.argv[3]
build_extras = len(sys.argv) == 5 and sys.argv[4] == 'build_extras'

supported_configs = ['Debug', 'Sanitize', 'Profile', 'Release', 'All']
if in_config not in supported_configs:
	print('Invalid config specified. Must be one of: ' + ', '.join(supported_configs))
	print_usage()
	exit(1)

supported_platforms = ['windows', 'linux']
if platform not in supported_platforms:
	print('Invalid platform specified. Must be one of: ' + ', '.join(supported_platforms))
	print_usage()
	exit(1)

git_path = git_path_windows if platform == 'windows' else git_path_linux
cmake_path = cmake_path_windows if platform == 'windows' else cmake_path_linux
genie_path = genie_path_windows if platform == 'windows' else genie_path_linux
python_path = python_path_windows if platform == 'windows' else python_path_linux

ps = subprocess.Popen(('cmake', '--version'), stdout=subprocess.PIPE)
cmake_version = str(ps.stdout.read())
ps.wait()
cmake_version = cmake_version.split('\\n')[0]
cmake_version = cmake_version.split(' ')[-1]
if '-' in cmake_version:
	cmake_version = cmake_version.split('-')[0]


(cmake_version_maj, cmake_version_min, cmake_version_pat) = map(int, cmake_version.split('.'))
print('Detected cmake version {0}.{1}.{2}'.format(cmake_version_maj, cmake_version_min, cmake_version_pat))

if cmake_version_maj < 3 or cmake_version_min < 13:
	print('Flex requires cmake 3.13 or higher, please update then retry building.')
	exit(1)

start_time = time.perf_counter()


def build_project(config):
	project_root = '../FlexEngine/'
	libs_target = project_root + 'lib/x64/' + config + '/'
	external_config = 'Debug' if (config == 'Debug' or config == 'Sanitize') else 'Release'

	print("\nBuilding " + config + " config (external: " + external_config + ")\n\n");

	if not os.path.exists(project_root + 'lib/x64/' + config):
		os.makedirs(project_root + 'lib/x64/' + config)

	print("\n------------------------------------------\n\nBuilding GLFW...\n\n------------------------------------------\n")

	# GLFW
	glfw_path = project_root + 'dependencies/glfw/'
	glfw_build_path = glfw_path + 'build/'
	if not os.path.exists(glfw_build_path):
		os.makedirs(glfw_build_path)

	glfw_cmake_args = ['-DGLFW_BUILD_EXAMPLES=OFF', '-DGLFW_BUILD_TESTS=OFF', '-DGLFW_BUILD_DOCS=OFF']
	if platform == 'linux':
		glfw_cmake_args += ['-G', 'Unix Makefiles', '.']

	# if config == 'Debug':
		# glfw_cmake_args += ['-DCMAKE_BUILD_TYPE=Debug']
	# else:
		# glfw_cmake_args += ['-DCMAKE_BUILD_TYPE=Release'] # TODO: Investigate MinSizeRel
	run_cmake(glfw_path, glfw_build_path, glfw_cmake_args)

	if platform == 'windows':
		glfw_msbuild_args = ['/property:Configuration=' + external_config]
		glfw_lib_path = glfw_build_path + 'src/' + config + '/'
		run_msbuild(glfw_build_path + 'glfw.sln', glfw_msbuild_args)
		shutil.copyfile(glfw_lib_path + 'glfw3.lib', libs_target + 'glfw3.lib')
	else:
		run_make(glfw_build_path, False)
		glfw_lib_path = glfw_build_path + 'src/'
		shutil.copyfile(glfw_lib_path + 'libglfw3.a', libs_target + 'libglfw3.a')

	# OpenAL
	if platform == 'windows':
		print("\n------------------------------------------\n\nBuilding OpenAL...\n\n------------------------------------------\n")
		openAL_path = project_root + 'dependencies/openAL/'
		openAL_build_path = openAL_path + 'build/'
		if not os.path.exists(openAL_build_path):
			os.makedirs(openAL_build_path)
		run_cmake(openAL_path, openAL_build_path, ['-DALSOFT_EXAMPLES=OFF', '-DALSOFT_TESTS=OFF', '-Wno-dev'])
		openal_msbuild_args = ['/property:Configuration=' + external_config] # TODO: Investigate MinSizeRel
		run_msbuild(openAL_build_path + 'openAL.sln', openal_msbuild_args)
		openal_lib_path = openAL_build_path + config
		shutil.copyfile(openal_lib_path + '/common.lib', libs_target + 'common.lib')
		shutil.copyfile(openal_lib_path + '/OpenAL32.dll', libs_target + 'OpenAL32.dll')
		shutil.copyfile(openal_lib_path + '/OpenAL32.lib', libs_target + 'OpenAL32.lib')

	print("\n------------------------------------------\n\nBuilding Bullet...\n\n------------------------------------------\n")

	# Bullet
	bullet_path = project_root + 'dependencies/bullet/'
	bullet_build_path = bullet_path + 'build/'
	if not os.path.exists(bullet_build_path):
		os.makedirs(bullet_build_path)
	bullet_cmake_args = [
		'-DUSE_MSVC_RUNTIME_LIBRARY_DLL=ON',
		'-DBUILD_UNIT_TESTS=OFF',
		'-DBUILD_CPU_DEMOS=OFF',
		'-DBUILD_BULLET2_DEMOS=OFF',
		'-DBUILD_EXTRAS=OFF',
		'-Wno-dev']
	# TODO: Necessary?
	bullet_cmake_args += ['-DCMAKE_BUILD_TYPE=' + external_config]
	run_cmake(bullet_path, bullet_build_path, bullet_cmake_args)
	if platform == 'windows':
		bullet_msbuild_args = ['/property:Configuration=' + external_config] # TODO: Investigate MinSizeRel
		run_msbuild(bullet_build_path + 'BULLET_PHYSICS.sln', bullet_msbuild_args)
		if external_config == 'Debug':
			shutil.copyfile(bullet_build_path + 'lib/Debug/BulletCollision_Debug.lib', libs_target + 'BulletCollision_Debug.lib')
			shutil.copyfile(bullet_build_path + 'lib/Debug/BulletDynamics_Debug.lib', libs_target + 'BulletDynamics_Debug.lib')
			shutil.copyfile(bullet_build_path + 'lib/Debug/LinearMath_Debug.lib', libs_target + 'LinearMath_Debug.lib')
		else:
			shutil.copyfile(bullet_build_path + 'lib/Release/BulletCollision.lib', libs_target + 'BulletCollision.lib')
			shutil.copyfile(bullet_build_path + 'lib/Release/BulletDynamics.lib', libs_target + 'BulletDynamics.lib')
			shutil.copyfile(bullet_build_path + 'lib/Release/LinearMath.lib', libs_target + 'LinearMath.lib')
	else:
		run_make(bullet_build_path, False)
		shutil.copyfile(bullet_build_path + 'src/BulletCollision/libBulletCollision.a', libs_target + 'libBulletCollision.a')
		shutil.copyfile(bullet_build_path + 'src/BulletDynamics/libBulletDynamics.a', libs_target + 'libBulletDynamics.a')
		shutil.copyfile(bullet_build_path + 'src/LinearMath/libLinearMath.a', libs_target + 'libLinearMath.a')

	if build_extras:
		print("\n------------------------------------------\n\nBuilding Bullet Full...\n\n------------------------------------------\n")

		# Bullet Full (Build demos & extras, not for use in Flex, but for inspecting separately)
		bullet_path = project_root + 'dependencies/bullet/'
		bullet_build_path = bullet_path + 'build_full/'
		if not os.path.exists(bullet_build_path):
			os.makedirs(bullet_build_path)
		run_cmake(bullet_path, bullet_build_path, ['-DUSE_MSVC_RUNTIME_LIBRARY_DLL=ON', '-DBUILD_UNIT_TESTS=OFF', '-DBUILD_CPU_DEMOS=ON', '-DBUILD_BULLET2_DEMOS=ON', '-DBUILD_EXTRAS=ON'])
		if platform == 'windows':
			run_msbuild(bullet_build_path + 'BULLET_PHYSICS.sln')
		else:
			run_make(bullet_build_path, False)

	print("\n------------------------------------------\n\nBuilding FreeType...\n\n------------------------------------------\n")

	# FreeType
	free_type_path = project_root + 'dependencies/freetype/'
	free_type_build_path = free_type_path
	if platform == 'linux':
		 free_type_build_path += 'build/'
	if not os.path.exists(free_type_build_path):
		os.makedirs(free_type_build_path)

	if platform == 'windows':
		freetype_msbuid_args = ['/property:Configuration=' + external_config, '/property:Platform=x64']
		run_msbuild(free_type_path + 'builds/windows/vc2010/freetype.sln', freetype_msbuid_args)
		shutil.copyfile(free_type_build_path + 'objs/x64/' + config + '/freetype.lib', libs_target + 'freetype.lib')
		shutil.copyfile(free_type_build_path + 'objs/x64/' + config + '/freetype.pdb', libs_target + 'freetype.pdb')
	else:
		subprocess.check_call('cd ' + free_type_path + '; dos2unix autogen.sh ; ./autogen.sh', stderr=subprocess.STDOUT, shell=True)
		run_cmake(free_type_path, free_type_build_path, [
			'-DCMAKE_DISABLE_FIND_PACKAGE_HarfBuzz=ON',
			'-Wno-dev'])
		run_make(free_type_build_path, False)
		shutil.copyfile(free_type_build_path + 'libfreetype.a', libs_target + 'libfreetype.a')

	print("\n------------------------------------------\n\nBuilding Shaderc...\n\n------------------------------------------\n")

	# Shaderc
	shader_c_path = project_root + 'dependencies/shaderc/'
	shader_c_build_path = shader_c_path + 'build/'

	# Delete existing directory if on incorrect commit
	del_shader_c_dir = os.path.exists(shader_c_path)

	if del_shader_c_dir:
		# Directory exists, check if on correct commit
		pdir = os.getcwd()
		os.chdir(shader_c_path)
		subprocess.check_output(["git", "fetch", "--tags"]).strip()
		curr_shader_c_commit = subprocess.check_output(["git", "describe", "--always"]).strip()
		curr_shader_c_commit = curr_shader_c_commit.decode()
		os.chdir(pdir)
		if curr_shader_c_commit == shaderc_tag:
			# No need to obliterate dir, just rebuild
			del_shader_c_dir = False
		print('On commit ' + str(curr_shader_c_commit) + ', required commit: ' + shaderc_tag + 
			(' (not matched, re-cloning repo)' if del_shader_c_dir else ' (matched, skipping clone of repo)'))

	if del_shader_c_dir:
		 # Remove existing shaderc directory (shutil.rmtree fails here with permission errors on windows, use the big guns instead)
		if platform == 'windows':
			os.system('rmdir /S/ Q "{}"'.format(shader_c_path))
		else:
			os.system('rm -rdf "{}"'.format(shader_c_path))

	if not os.path.exists(shader_c_path):
		run_git(['clone', 'https://github.com/google/shaderc', shader_c_path, '--recurse-submodules', '--depth=1'])

		if platform == 'windows':
			pdir = os.getcwd()
			os.chdir(shader_c_path)
			run_git(['fetch', '--tags'])
			run_git(['checkout', 'tags/' + shaderc_tag, '-b', 'master'])
			os.chdir(pdir)
		else:
			pdir = os.getcwd()
			os.chdir(shader_c_path)
			run_git(['fetch', '--tags'])
			run_git(['checkout', 'tags/' + shaderc_tag, '-b', 'master'])
			os.chdir(pdir)

	if os.path.exists(shader_c_build_path):
		shutil.rmtree(shader_c_build_path)
	else:
		os.makedirs(shader_c_build_path)

	if platform == 'windows':
		os.environ['GIT_EXECUTABLE'] = git_path
	subprocess.check_call([python_path, shader_c_path + 'utils/git-sync-deps'], stderr=subprocess.STDOUT)

	shader_c_cmake_args = [
		'-DSHADERC_ENABLE_SPVC=ON',
		'-DSHADERC_SKIP_TESTS=ON',
		'-DBUILD_TESTING=OFF',
		'-DENABLE_CTEST=OFF',
		'-DSHADERC_ENABLE_SHARED_CRT=ON',
		'-Wno-dev']
	if platform == 'linux':
		shader_c_cmake_args += ['-G', 'Unix Makefiles', '.']

	shader_c_cmake_args += ['-DCMAKE_BUILD_TYPE=' + external_config]

	run_cmake(shader_c_path, shader_c_build_path, shader_c_cmake_args)
	run_cmake_build(platform, shader_c_build_path, ['--config', external_config])

	if platform == 'windows':
		shader_c_lib_path = shader_c_build_path + 'libshaderc/' + config + '/'
		shutil.copyfile(shader_c_lib_path + 'shaderc_combined.lib', libs_target + 'shaderc_combined.lib')
	else:
		shader_c_lib_path = shader_c_build_path + 'libshaderc/'
		shutil.copyfile(shader_c_lib_path + 'libshaderc_combined.a', libs_target + 'libshaderc_combined.a')


print("\nBuilding Flex Engine..." + ("(with extras)" if build_extras else "") +  "\n");

if in_config == 'All':
	for c in supported_configs[:-1]:
		build_project(c)
else:
	build_project(in_config)


print("\n------------------------------------------\n\nBuilding genie project...\n\n------------------------------------------\n")

# Project
subprocess.check_call([genie_path, '--file=genie.lua', genie_target], stderr=subprocess.STDOUT)

end_time = time.perf_counter()
total_elapsed = end_time - start_time
print("Building all Flex dependencies took {0:0.1f} sec ({1:0.1f} min)".format(total_elapsed, total_elapsed / 60.0))
