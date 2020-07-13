import os
import subprocess
import shutil
import importlib.util

msbuild_path = 'C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe'
git_path = 'C:\\Program Files\\Git\\bin\\git.exe'
cmake_path = 'cmake.exe'

def run_cmake(source, build, arguments = []):
    cmakeCmd = [cmake_path, '-S', source, '-B', build]
    cmakeCmd += arguments
    subprocess.check_call(cmakeCmd, stderr=subprocess.STDOUT, shell=True)

def run_cmake_build(path):
    subprocess.check_call([cmake_path, '--build', path], stderr=subprocess.STDOUT, shell=True)

def run_msbuild(sln, arguments = []):
    cmd = [msbuild_path, sln]
    cmd += arguments
    subprocess.check_call(cmd, stderr=subprocess.STDOUT, shell=True)

def run_git(arguments = []):
    cmd = [git_path] + arguments
    subprocess.check_call(cmd, stderr=subprocess.STDOUT, shell=True)

print("Building Flex Engine...");

project_root = 'FlexEngine/'
libs_target = project_root + 'lib/x64/Debug/'

if not os.path.exists(project_root + 'lib/x64/Debug'):
    os.makedirs(project_root + 'lib/x64/Debug')

#GLFW
glfw_path = project_root + 'dependencies/glfw/'
glfw_build_path = glfw_path + 'build/'
if not os.path.exists(glfw_build_path):
    os.makedirs(glfw_build_path)
run_cmake(glfw_path, glfw_build_path, ['-DGLFW_BUILD_EXAMPLES=OFF', '-DGLFW_BUILD_TESTS=OFF' , '-DGLFW_BUILD_DOCS=OFF'])
run_msbuild(glfw_build_path + 'glfw.sln')
shutil.copyfile(glfw_build_path + '/src/Debug/glfw3.lib', libs_target + 'glfw3.lib')

#OpenAL
openAL_path = project_root + 'dependencies/openAL/'
openAL_build_path = openAL_path + 'build/'
if not os.path.exists(openAL_build_path):
    os.makedirs(openAL_build_path)
run_cmake(openAL_path, openAL_build_path, ['-DALSOFT_EXAMPLES=OFF', '-DALSOFT_TESTS=OFF'])
run_msbuild(openAL_build_path + 'openAL.sln')
shutil.copyfile(openAL_build_path + 'Debug/common.lib', libs_target + 'common.lib')
shutil.copyfile(openAL_build_path + 'Debug/OpenAL32.dll', libs_target + 'OpenAL32.dll')
shutil.copyfile(openAL_build_path + 'Debug/OpenAL32.lib', libs_target + 'OpenAL32.lib')

#Bullet
bullet_path = project_root + 'dependencies/bullet/'
bullet_build_path = bullet_path + 'build/'
if not os.path.exists(bullet_build_path):
    os.makedirs(bullet_build_path)
run_cmake(bullet_path, bullet_build_path, ['-DUSE_MSVC_RUNTIME_LIBRARY_DLL=ON', '-DBUILD_UNIT_TESTS=OFF', '-DBUILD_CPU_DEMOS=OFF', '-DBUILD_BULLET2_DEMOS=OFF', '-DBUILD_EXTRAS=OFF'])
run_msbuild(bullet_build_path + 'BULLET_PHYSICS.sln')
shutil.copyfile(bullet_build_path + 'lib/Debug/BulletCollision_Debug.lib', libs_target + 'BulletCollision_Debug.lib')
shutil.copyfile(bullet_build_path + 'lib/Debug/BulletDynamics_Debug.lib', libs_target + 'BulletDynamics_Debug.lib')
shutil.copyfile(bullet_build_path + 'lib/Debug/LinearMath_Debug.lib', libs_target + 'LinearMath_Debug.lib')

#FreeType
free_type_path = project_root + 'dependencies/freetype/'
free_type_build_path = free_type_path
if not os.path.exists(free_type_build_path):
    os.makedirs(free_type_build_path)
run_msbuild(free_type_path + 'builds/windows/vc2010/freetype.sln', ['/property:Configuration=Debug Static', '/property:Platform=x64'])
shutil.copyfile(free_type_build_path + 'objs/x64/Debug Static/freetype.lib', libs_target + 'freetype.lib')
shutil.copyfile(free_type_build_path + 'objs/x64/Debug Static/freetype.pdb', libs_target + 'freetype.pdb')

#Shaderc
shader_c_path = project_root + 'dependencies/shaderc_test/'
shader_c_build_path = shader_c_path + 'build/'
if not os.path.exists(shader_c_path):
    run_git(['clone', 'https://github.com/google/shaderc', shader_c_path, '--recurse-submodules'])
if not os.path.exists(shader_c_build_path):
    os.makedirs(shader_c_build_path)

os.environ['GIT_EXECUTABLE'] = git_path
subprocess.check_call(['python', shader_c_path + 'utils/git-sync-deps'], stderr=subprocess.STDOUT, shell=True)
run_cmake(shader_c_path, shader_c_build_path, ['-DBUILD_GMOCK=OFF', '-DBUILD_TESTING=OFF', '-DENABLE_BUILD_SAMPLES=OFF', '-DENABLE_CTEST=OFF', '-DINSTALL_GTEST=OFF', '-DSHADERC_ENABLE_SHARED_CRT=ON', '-DLLVM_USE_CRT_DEBUG=MDd', '-DLLVM_USE_CRT_MINSIZEREL=MD', '-DLLVM_USE_CRT_RELEASE=MD', '-DLLVM_USE_CRT_RELWITHDEBINFO=MD'])
run_cmake_build(shader_c_build_path)
shutil.copyfile(shader_c_build_path + 'libshaderc/Debug/shaderc_combined.lib', libs_target + 'shaderc_combined.lib')

#Project
subprocess.check_call(['genie.exe', '--file=scripts/genie.lua', 'vs2019'], stderr=subprocess.STDOUT, shell=True)
