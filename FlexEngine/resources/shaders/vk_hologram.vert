#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec3 in_Normal;

layout (location = 0) out vec3 ex_NormalWS;

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;

	vec4 constEmissive;
} uboDynamic;

void main()
{
	ex_NormalWS = normalize(mat3(uboDynamic.model) * in_Normal);

    vec4 worldPos = uboDynamic.model * vec4(in_Position, 1.0);
    gl_Position = uboConstant.viewProjection * worldPos;
}
