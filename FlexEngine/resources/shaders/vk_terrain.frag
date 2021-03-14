#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
} uboConstant;

layout (location = 0) in vec2 ex_TexCoord;
layout (location = 1) in vec4 ex_Colour;
layout (location = 2) in vec3 ex_NormalWS;
layout (location = 3) in vec3 ex_PositionWS;

layout (binding = 2) uniform sampler2D albedoSampler;

layout (location = 0) out vec4 fragmentColour;

void main() 
{
	vec3 albedo = ex_Colour.rgb * texture(albedoSampler, ex_TexCoord).rgb;
	vec3 N = normalize(ex_NormalWS);

	mat4 invView = inverse(uboConstant.view);
	vec3 camPos = vec3(invView[3][0], invView[3][1], invView[3][2]);

	float dist = clamp(length(camPos - ex_PositionWS)*0.0002,0.15,1.0);

	dist = smoothstep(dist, 0.0, 0.1);

	vec3 V = normalize(camPos.xyz - ex_PositionWS);

	vec3 L = normalize(vec3(0.57, 0.57, 0.57));
	float light = dot(N, L) * 0.5 + 0.5;
	float fresnel = pow(1.0 - dot(N, V), 6.0);


	vec3 groundCol;

	float min = 0.45;
	float max = 0.52;
	vec3 lowCol = vec3(0.13, 0.11, 0.041);
	vec3 highCol = vec3(0.08, 0.16, 0.04);// vec3(0.00, 0.04, 0.01);
	if (ex_Colour.r > max)
	{
		groundCol = highCol;
	}
	else if (ex_Colour.r < min)
	{
		groundCol = lowCol;
	}
	else
	{
		float alpha = (ex_Colour.r - min) / (max - min);
		groundCol = mix(lowCol, highCol, clamp(alpha, 0.0, 1.0));
	}
	groundCol *= light;
	groundCol += (fresnel * 1.2) * groundCol;
	fragmentColour = vec4(mix(groundCol, vec3(0), pow(dist, 0.2)), 1.0);
	// fragmentColour = vec4(ex_Colour.rgb, 1.0);
}
