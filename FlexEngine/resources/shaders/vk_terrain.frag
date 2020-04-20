#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
} uboConstant;

layout (location = 0) in vec2 ex_TexCoord;
layout (location = 1) in vec4 ex_Color;
layout (location = 2) in vec3 ex_NormalWS;

layout (binding = 0) uniform sampler2D albedoSampler;

layout (location = 0) out vec4 fragmentColor;

void main() 
{
	vec3 albedo = ex_Color.rgb * texture(albedoSampler, ex_TexCoord).rgb;
	vec3 N = normalize(ex_NormalWS);

	fragmentColor = vec4(N*0.5+0.5, 1.0);
}
