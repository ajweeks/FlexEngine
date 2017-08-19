#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec3 inTangent;
layout (location = 3) in vec3 inBitangent;
layout (location = 4) in vec3 inNormal;
layout (location = 5) in vec2 inTexCoord;

// Updated once per frame
layout (binding = 0) uniform UBO
{
	mat4 projection;
	mat4 view;
	vec4 camPos; // Padded to 16 bytes (only 3 floats needed)
	vec4 lightDir; // Padded to 16 bytes (only 3 floats needed)
	vec4 ambientColor;
	vec4 specularColor;
} ubo;

// Updated once per object
layout (binding = 1) uniform UBOInstance
{
	mat4 model;
	mat4 modelInvTranspose;
	bool useDiffuseTexture;
	bool useNormalTexture;
	bool useSpecularTexture;
} uboInstance;

layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec4 outColor;
layout (location = 2) out mat3 outTBN;
layout (location = 5) out vec2 outTexCoord;

out gl_PerVertex 
{
	vec4 gl_Position;
};

void main() 
{
	outWorldPos = vec3(uboInstance.model * vec4(inWorldPos, 1.0));
	gl_Position = ubo.projection * ubo.view * vec4(outWorldPos, 1.0);

	outColor = inColor;
	outTBN = mat3(
		normalize(mat3(uboInstance.modelInvTranspose) * inTangent),
		normalize(mat3(uboInstance.modelInvTranspose) * inBitangent),
		normalize(mat3(uboInstance.modelInvTranspose) * inNormal));
	outTexCoord = inTexCoord;
	
	// Convert from GL coordinates to Vulkan coordinates
	// TODO: Move out to external function in helper file
	gl_Position.y = -gl_Position.y;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
