#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec2 inTexCoord;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outTexCoord;

layout (binding = 0) uniform UboView 
{
	mat4 projection;
	mat4 view;
} uboView;

layout (binding = 1) uniform UboInstance 
{
	mat4 model;
} uboInstance;


out gl_PerVertex 
{
	vec4 gl_Position;   
};

void main() 
{
	mat4 modelView = uboView.view * uboInstance.model;
	vec3 worldPos = vec3(modelView * vec4(inPos, 1.0));
	gl_Position = uboView.projection * modelView * vec4(inPos.xyz, 1.0);

	outColor = inColor;
	outNormal = mat3(transpose(inverse(uboInstance.model))) * inNormal;
	outTexCoord = inTexCoord;
	
	// Convert from GL coordinates to Vulkan coordinates
	// TODO: Move out to external function in helper file
	gl_Position.y = -gl_Position.y;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
