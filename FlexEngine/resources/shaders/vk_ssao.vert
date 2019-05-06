#version 450

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;

layout (location = 0) out vec2 ex_UV;

const int SSAO_KERNEL_SIZE = 64;

layout (binding = 0) uniform UBO 
{
	mat4 invView;
	mat4 projection;
	mat4 invProj;
	vec4 samples[SSAO_KERNEL_SIZE];
} uboConstant;

void main()
{
	ex_UV = in_TexCoord;
    gl_Position = vec4(in_Position, 1.0);
}
