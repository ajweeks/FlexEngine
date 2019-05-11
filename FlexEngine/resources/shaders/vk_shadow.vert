#version 450

layout (binding = 0) uniform UBOConstant
{
	mat4 lightViewProj;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

layout (location = 0) in vec3 in_Position;

void main() 
{
	gl_Position = uboConstant.lightViewProj * uboDynamic.model * vec4(in_Position, 1.0);
}
