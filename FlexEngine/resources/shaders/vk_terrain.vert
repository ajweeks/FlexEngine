#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 2) in vec4 in_Color;
layout (location = 3) in vec3 in_Normal;

layout (location = 0) out vec2 ex_TexCoord;
layout (location = 1) out vec4 ex_Color;
layout (location = 2) out vec3 ex_NormalWS;
layout (location = 3) out vec3 ex_PositionWS;

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

void main()
{
	ex_TexCoord = in_TexCoord;
	ex_Color = in_Color;

	ex_NormalWS = mat3(uboDynamic.model) * in_Normal;

    vec4 worldPos = uboDynamic.model * vec4(in_Position, 1.0);
	ex_PositionWS = worldPos.xyz;
    gl_Position = uboConstant.viewProjection * worldPos;
}
