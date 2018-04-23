#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 inWorldPos;
layout (location = 1) in vec4 inColor;

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 viewProjection;
} uboConstant;

// Updated once per object
layout (binding = 1) uniform UBODynamic
{
	mat4 model;
	vec4 multiplier;
} uboDynamic;

layout (location = 0) out vec4 outColor;

out gl_PerVertex 
{
	vec4 gl_Position;
};

void main() 
{
	gl_Position = uboConstant.viewProjection * uboDynamic.model * inWorldPos;

	outColor = inColor * uboDynamic.multiplier;
	
	// Convert from GL coordinates to Vulkan coordinates
	// TODO: Move out to external function in helper file
	gl_Position.y = -gl_Position.y;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
