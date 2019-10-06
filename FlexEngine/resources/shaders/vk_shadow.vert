#version 450

layout (push_constant) uniform PushConstants
{
	layout (offset = 0) mat4 viewProj;
} pushConstants;

layout (binding = 0) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

layout (location = 0) in vec3 in_Position;

void main() 
{
	gl_Position = (pushConstants.viewProj * uboDynamic.model) * vec4(in_Position, 1.0);
}
