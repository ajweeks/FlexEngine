#!/usr/bin/env python3

import os
import subprocess
import shutil
import time
import sys


def print_usage():
	print('Usage: "python ' + sys.argv[0] + ' [platform] [config]"\n'
			'e.g: "python ' + sys.argv[0] + ' windows Debug"')


if len(sys.argv) != 3:
	print('Invalid number of arguments (' + str(len(sys.argv)) + ')')
	print_usage()
	exit(1)

in_platform = sys.argv[1]
in_config = sys.argv[2]

supported_configs = ['Debug', 'Sanitize', 'Profile', 'Release', 'All']
if in_config not in supported_configs:
	print('Invalid config specified. Must be one of: ' + ', '.join(supported_configs))
	print_usage()
	exit(1)

supported_platforms = ['windows', 'linux', 'All']
if in_platform not in supported_platforms:
	print('Invalid platform specified. Must be one of: ' + ', '.join(supported_platforms))
	print_usage()
	exit(1)

start_time = time.perf_counter()


def rm_dir(dir):
	if os.path.exists(dir):
		shutil.rmtree(dir)


def rm_file(file):
	if os.path.exists(file):
		os.remove(file)


def clean_project(config, platform):
	project_root = '../FlexEngine/'
	libs_target = project_root + 'lib/x64/' + config + '/'
	external_config = 'Debug' if (config == 'Debug' or config == 'Sanitize') else 'Release'

	print("\nCleaning config: " + config + ", platform: " + platform + "\n");

	print("Cleaning GLFW...")

	# GLFW
	glfw_path = project_root + 'dependencies/glfw/'
	rm_dir(glfw_path + 'build/')
	rm_dir(glfw_path + 'out/')

	if platform == 'windows':
		rm_file(libs_target + 'glfw3.lib')
	else:
		rm_file(libs_target + 'libglfw3.a')

	# OpenAL
	if platform == 'windows':
		print("Cleaning OpenAL...")
		openAL_path = project_root + 'dependencies/openAL/'
		rm_dir(openAL_path + 'build/')
		rm_file(libs_target + 'common.lib')
		rm_file(libs_target + 'OpenAL32.dll')
		rm_file(libs_target + 'OpenAL32.lib')
	# TODO: Handle linux?

	print("Cleaning Bullet...")

	# Bullet
	bullet_path = project_root + 'dependencies/bullet/'
	rm_dir(bullet_path + 'build/')
	if platform == 'windows':
		if external_config == 'Debug':
			rm_file(libs_target + 'BulletCollision_Debug.lib')
			rm_file(libs_target + 'BulletDynamics_Debug.lib')
			rm_file(libs_target + 'LinearMath_Debug.lib')
		else:
			rm_file(libs_target + 'BulletCollision.lib')
			rm_file(libs_target + 'BulletDynamics.lib')
			rm_file(libs_target + 'LinearMath.lib')
	else:
		rm_file(libs_target + 'libBulletCollision.a')
		rm_file(libs_target + 'libBulletDynamics.a')
		rm_file(libs_target + 'libLinearMath.a')

	print("Cleaning FreeType...")

	# FreeType
	free_type_path = project_root + 'dependencies/freetype/'
	free_type_build_path = free_type_path
	if platform == 'linux':
		 free_type_build_path += 'build/'
	# TODO: What do here?
	# rm_dir(free_type_build_path)

	if platform == 'windows':
		rm_file(libs_target + 'freetype.lib')
		rm_file(libs_target + 'freetype.pdb')
	else:
		rm_file(libs_target + 'libfreetype.a')

	print("Cleaning Shaderc...")

	# Shaderc
	shader_c_path = project_root + 'dependencies/shaderc/'
	shader_c_build_path = shader_c_path + 'build/'

	rm_dir(shader_c_build_path)

	if platform == 'windows':
		rm_file(libs_target + 'shaderc_combined.lib')
	else:
		rm_file(libs_target + 'libshaderc_combined.a')


print("\nCleaning Flex dependencies...");

if in_config == 'All':
	if in_platform == 'All':
		for (config, platform) in [(c, p) for c in supported_configs[:-1] for p in supported_platforms[:-1]]:
			clean_project(config, platform)
	else:
		for config in supported_configs[:-1]:
			clean_project(config, in_platform)
else:
	if in_platform == 'All':
		for platform in supported_platforms[:-1]:
			clean_project(in_config, platform)
	else:
		clean_project(in_config, in_platform)


end_time = time.perf_counter()
total_elapsed = end_time - start_time
print("Cleaning Flex dependencies took {0:0.1f} sec ({1:0.1f} min)".format(total_elapsed, total_elapsed / 60.0))
