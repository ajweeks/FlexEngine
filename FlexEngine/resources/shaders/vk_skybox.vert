#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;

layout (push_constant) uniform PushConstants
{
	layout (offset = 0) mat4 view;
	layout (offset = 64) mat4 proj;
} pushConstants;

layout (location = 0) out vec3 ex_TexCoord;

out gl_PerVertex 
{
	vec4 gl_Position;
};

void main() 
{
	ex_TexCoord = in_Position;
	vec4 pos = pushConstants.proj * mat4(mat3(pushConstants.view)) * vec4(in_Position, 1.0);
	gl_Position = pos;

	// Push to far clip plane
	gl_Position.z = 1.0e-9f;
}
