#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;

layout (location = 0) out vec3 WorldPos;

layout (push_constant) uniform PushConstantBlock
{
	mat4 mvp;
} pushConstantBlock;

void main()
{
    WorldPos = in_Position;

    // Remove translation part from view matrix to keep it centered around viewer
	vec4 clipPos = pushConstantBlock.mvp * vec4(in_Position, 1.0);

	gl_Position = clipPos.xyww;
	
	// Convert from GL coordinates to Vulkan coordinates
	// TODO: Move out to external function in helper file
	gl_Position.y = -gl_Position.y;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
