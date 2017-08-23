#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;

// Updated once per frame
layout (binding = 0) uniform UBO
{
	mat4 projection;
	mat4 view;
} ubo;

// Updated once per object
layout (binding = 1) uniform UBOInstance
{
	mat4 model;
	bool useCubemapTexture;
} uboInstance;

layout (location = 0) out vec3 ex_TexCoord;

out gl_PerVertex 
{
	vec4 gl_Position;
};

void main() 
{
	ex_TexCoord = in_Position;
	// Truncate translation part of view matrix to center skybox around viewport
	vec4 pos = ubo.projection * mat4(mat3(ubo.view)) * uboInstance.model * vec4(in_Position, 1.0);
	gl_Position = pos.xyww; // Clip coords to NDC to ensure skybox is rendered at far plane

	// Convert from GL coordinates to Vulkan coordinates
	// TODO: Move out to external function in helper file
	gl_Position.y = -gl_Position.y;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
