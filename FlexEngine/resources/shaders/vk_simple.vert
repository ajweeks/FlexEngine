#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec2 inTexCoord;
layout (location = 2) in vec4 inColor;
layout (location = 3) in vec3 inTangent;
layout (location = 4) in vec3 inBitangent;
layout (location = 5) in vec3 inNormal;

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 projection;
	mat4 view;
	vec4 camPos; // Padded to 16 bytes (only 3 floats needed)
	vec4 lightDir; // Padded to 16 bytes (only 3 floats needed)
	vec4 ambientColor;
	vec4 specularColor;
} ubo;

// Updated once per object
layout (binding = 1) uniform UBODynamic
{
	mat4 model;
	mat4 modelInvTranspose;
	bool useDiffuseTexture;
	bool useNormalTexture;
	bool useSpecularTexture;
} uboDynamic;

layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec2 outTexCoord;
layout (location = 2) out vec4 outColor;
layout (location = 3) out mat3 outTBN;

out gl_PerVertex 
{
	vec4 gl_Position;
};

void main() 
{
	outWorldPos = vec3(uboDynamic.model * vec4(inWorldPos, 1.0));
	gl_Position = ubo.projection * ubo.view * vec4(outWorldPos, 1.0);

	outColor = inColor;
	outTBN = mat3(
		normalize(mat3(uboDynamic.modelInvTranspose) * inTangent),
		normalize(mat3(uboDynamic.modelInvTranspose) * inBitangent),
		normalize(mat3(uboDynamic.modelInvTranspose) * inNormal));
	outTexCoord = inTexCoord;
}
