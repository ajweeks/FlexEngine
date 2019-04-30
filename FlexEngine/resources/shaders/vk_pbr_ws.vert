#version 450

// Deferred PBR

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 2) in vec3 in_Normal;
layout (location = 3) in vec3 in_Tangent;

layout (location = 0) out vec3 ex_WorldPos;
layout (location = 1) out vec2 ex_TexCoord;
layout (location = 2) out mat3 ex_TBN;

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
} uboConstant;

// Updated once per object
layout (binding = 1) uniform UBODynamic
{
	mat4 model;

	// Constant values to use when not using samplers
	vec4 constAlbedo;
	float constMetallic;
	float constRoughness;

	// PBR samplers
	bool enableAlbedoSampler;
	bool enableMetallicSampler;
	bool enableRoughnessSampler;
	bool enableNormalSampler;
	
	float blendSharpness;
	float textureScale;
} uboDynamic;

void main()
{
	ex_TexCoord = in_TexCoord;

	vec3 bitan = cross(in_Normal, in_Tangent);
	ex_TBN = mat3(
		normalize(mat3(uboDynamic.model) * in_Tangent), 
		normalize(mat3(uboDynamic.model) * bitan), 
		normalize(mat3(uboDynamic.model) * in_Normal));

    vec4 worldPos = uboDynamic.model * vec4(in_Position, 1.0);
    ex_WorldPos = worldPos.xyz;
    gl_Position = uboConstant.viewProjection * worldPos;
}
