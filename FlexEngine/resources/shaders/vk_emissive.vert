#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// TODO: Share with pbr.vert

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 2) in vec4 in_Colour;
layout (location = 3) in vec3 in_Normal;
layout (location = 4) in vec3 in_Tangent;

layout (location = 0) out vec2 ex_TexCoord;
layout (location = 1) out vec4 ex_Colour;
layout (location = 2) out mat3 ex_TBN;

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;

	vec4 constAlbedo;
	vec4 constEmissive;
	float constRoughness;

	bool enableAlbedoSampler;
	bool enableNormalSampler;
	bool enableEmissiveSampler;
} uboDynamic;

void main()
{
	ex_TexCoord = in_TexCoord;
	ex_Colour = in_Colour;

	vec3 bitan = cross(in_Normal, in_Tangent);
	ex_TBN = mat3(
		normalize(mat3(uboDynamic.model) * in_Tangent), 
		normalize(mat3(uboDynamic.model) * bitan), 
		normalize(mat3(uboDynamic.model) * in_Normal));

    vec4 worldPos = uboDynamic.model * vec4(in_Position, 1.0);
    gl_Position = uboConstant.viewProjection * worldPos;
}
