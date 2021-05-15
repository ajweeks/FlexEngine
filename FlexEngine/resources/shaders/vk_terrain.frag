#version 450

#include "vk_misc.glsl"

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
	DirectionalLight dirLight;
	SkyboxData skyboxData;
	ShadowSamplingData shadowSamplingData;
} uboConstant;

layout (location = 0) in vec2 ex_TexCoord;
layout (location = 1) in vec4 ex_Colour;
layout (location = 2) in vec3 ex_NormalWS;
layout (location = 3) in vec3 ex_PositionWS;
layout (location = 4) in vec3 ex_PositionVS;

layout (binding = 2) uniform sampler2D albedoSampler;
layout (binding = 3) uniform sampler2DArray shadowMaps;

layout (location = 0) out vec4 fragmentColour;

void main()
{
	vec3 albedo = ex_Colour.rgb * texture(albedoSampler, ex_TexCoord).rgb;
	vec3 N = normalize(ex_NormalWS);
	float roughness = 1.0;
	float metallic = 0.0;
	float ssao = 0.0;

	mat4 invView = inverse(uboConstant.view);
	vec3 camPos = vec3(invView[3][0], invView[3][1], invView[3][2]);

	// TODO: Get proper linear depth
	float dist = clamp(length(camPos - ex_PositionWS)*0.0001 - 0.1,0.0,1.0);

	//dist = smoothstep(dist, 0.0, 0.13);

	vec3 V = normalize(camPos.xyz - ex_PositionWS);

	vec3 L = normalize(vec3(0.57, 0.57, 0.57));
	float light = dot(N, L) * 0.5 + 0.5;
	float fresnel = pow(1.0 - dot(N, V), 6.0);

	float linDepth = (1.0-ex_PositionVS.z)*.001;

	vec3 groundCol;

	float minHeight = 0.45;
	float maxHeight = 0.52;
	vec3 lowCol = pow(vec3(0.06, 0.05, 0.02), vec3(2.2));
	vec3 highCol = pow(vec3(0.04, 0.07, 0.02), vec3(2.2));// vec3(0.00, 0.04, 0.01);
	if (ex_Colour.r > maxHeight)
	{
		groundCol = highCol;
	}
	else if (ex_Colour.r < minHeight)
	{
		groundCol = lowCol;
	}
	else
	{
		float alpha = (ex_Colour.r - minHeight) / (maxHeight - minHeight);
		groundCol = mix(lowCol, highCol, clamp(alpha, 0.0, 1.0));
	}

	uint cascadeIndex = 0;
	for (uint i = 0; i < NUM_CASCADES; ++i)
	{
		if (linDepth > uboConstant.shadowSamplingData.cascadeDepthSplits[i])
		{
			cascadeIndex = i + 1;
		}
	}

	float dirLightShadowOpacity = 1.0;
	if (uboConstant.dirLight.enabled != 0)
	{
		vec3 L = normalize(uboConstant.dirLight.direction);
		vec3 radiance = uboConstant.dirLight.colour.rgb * uboConstant.dirLight.brightness;
		float NoL = max(dot(N, L), 0.0);

		dirLightShadowOpacity = DoShadowMapping(uboConstant.dirLight, uboConstant.shadowSamplingData, ex_PositionWS, cascadeIndex, shadowMaps, NoL);
		light *= (0.75 * dirLightShadowOpacity + 0.25);
		groundCol *= radiance;
	}

	groundCol *= light;
	vec3 diffuse = groundCol;
	vec3 specular = vec3(0);
	groundCol += (fresnel * 1.2) * groundCol;
	groundCol += (1.0 * max(dot(N, vec3(0, 1, 0)), 0.0)) * uboConstant.skyboxData.colourTop.rgb * groundCol;
	fragmentColour = vec4(mix(groundCol, uboConstant.skyboxData.colourFog.rgb, dist), 1.0);

	fragmentColour.rgb = fragmentColour.rgb / (fragmentColour.rgb + vec3(1.0f)); // Reinhard tone-mapping
	fragmentColour.rgb = pow(fragmentColour.rgb, vec3(1.0f / 2.2f)); // Gamma correction

    DrawDebugOverlay(albedo, N, roughness, metallic, diffuse, specular, ex_TexCoord,
     linDepth, dirLightShadowOpacity, ssao, /* inout */ fragmentColour);

	// fragmentColour.rgb *= ColourByShadowCascade(cascadeIndex);
	// fragmentColour = vec4(ex_Colour.rgb, 1.0);
	//fragmentColour = vec4(ex_TexCoord, 0.0, 1.0);
	// fragmentColour = vec4(N*0.5+0.5, 1.0);
}
