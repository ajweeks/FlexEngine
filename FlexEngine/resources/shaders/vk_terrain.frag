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
layout (location = 3) in vec3 ex_PositionWS;

layout (binding = 0) uniform sampler2D albedoSampler;

layout (location = 0) out vec4 fragmentColor;

void main() 
{
	vec3 albedo = ex_Color.rgb * texture(albedoSampler, ex_TexCoord).rgb;
	vec3 N = normalize(ex_NormalWS);

	mat4 invView = inverse(uboConstant.view);
	vec3 camPos = vec3(invView[3][0], invView[3][1], invView[3][2]);

	float dist = clamp(length(camPos - ex_PositionWS)*0.0002,0.15,1.0);

	dist = smoothstep(dist, 0.0, 0.1);

	fragmentColor = vec4(mix(ex_Color.rgb, vec3(0), pow(dist, 0.2)), 1.0);
}
