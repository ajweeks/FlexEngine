#version 450

#include "vk_misc.glsl"

// Deferred PBR Combine

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 fragColour;

layout (binding = 0) uniform UBOConstant
{
	vec4 camPos;
	mat4 invView;
	mat4 invProj;
	DirectionalLight dirLight;
	PointLight pointLights[NUM_POINT_LIGHTS];
	SpotLight spotLights[NUM_SPOT_LIGHTS];
	SkyboxData skyboxData;
	ShadowSamplingData shadowSamplingData;
	SSAOSamplingData ssaoData;
	float zNear;
	float zFar;
} uboConstant;

layout (binding = 1) uniform sampler2D brdfLUT;
layout (binding = 2) uniform samplerCube irradianceSampler;
layout (binding = 3) uniform samplerCube prefilterMap;
layout (binding = 4) uniform sampler2D depthBuffer;
layout (binding = 5) uniform sampler2D ssaoBuffer;
layout (binding = 6) uniform sampler2DArray shadowMaps;

layout (binding = 7) uniform sampler2D normalRoughnessTex;
layout (binding = 8) uniform sampler2D albedoMetallicTex;

void main()
{
    vec3 N = texture(normalRoughnessTex, ex_TexCoord).rgb;
    N = mat3(uboConstant.invView) * N; // view space -> world space
    //  TODO: Compile out as variant
    if (!gl_FrontFacing) N *= -1.0;
    float roughness = texture(normalRoughnessTex, ex_TexCoord).a;

    float depth = texture(depthBuffer, ex_TexCoord).r;
    vec3 posVS = ReconstructVSPosFromDepth(uboConstant.invProj, ex_TexCoord, depth);
    vec3 posWS = ReconstructWSPosFromDepth(uboConstant.invProj, uboConstant.invView, ex_TexCoord, depth);
    float depthN = posVS.z*(1/48.0);
	
    float invDist = 1.0f/(uboConstant.zFar-uboConstant.zNear);

	float linDepth = (posVS.z-uboConstant.zNear)*invDist;
	// fragColour = vec4(vec3(linDepth), 1); return;

    vec3 albedo = texture(albedoMetallicTex, ex_TexCoord).rgb;	
    float metallic = texture(albedoMetallicTex, ex_TexCoord).a;

    float ssao = (uboConstant.ssaoData.enabled == 1 ? texture(ssaoBuffer, ex_TexCoord).r : 1.0f);

	// fragColour = vec4(vec3(pow(ssao, uboConstant.ssaoPowExp)), 1); return;

	uint cascadeIndex = 0;
	for (uint i = 0; i < NUM_CASCADES; ++i)
	{
		if (linDepth > uboConstant.shadowSamplingData.cascadeDepthSplits[i])
		{
			cascadeIndex = i + 1;
		}
	}

	float fogDist = clamp(length(uboConstant.camPos.xyz - posWS)*0.0003 - 0.1,0.0,1.0);
	vec3 V = normalize(uboConstant.camPos.xyz - posWS);
	vec3 R = reflect(-V, N);

	float NoV = max(dot(N, V), 0.0);

	// If diaelectric, F0 should be 0.04, if metal it should be the albedo colour
	vec3 F0 = vec3(0.04);
	F0 = mix(F0, albedo, metallic);

	// Reflectance equation
	vec3 Lo = vec3(0.0);
	for (int i = 0; i < NUM_POINT_LIGHTS; ++i)
	{
		if (uboConstant.pointLights[i].enabled == 0)
		{
			continue;
		}

		float distance = length(uboConstant.pointLights[i].position - posWS);

		if (distance > 125)
		{
			// TODO: Define radius on point lights individually
			continue;
		}

		// Pretend point lights have a radius of 1cm to avoid division by 0
		float attenuation = 1.0 / max((distance * distance), 0.001);
		vec3 L = normalize(uboConstant.pointLights[i].position - posWS);
		vec3 radiance = uboConstant.pointLights[i].colour.rgb * attenuation * uboConstant.pointLights[i].brightness;
		float NoL = max(dot(N, L), 0.0);

		Lo += DoLighting(radiance, N, V, L, NoV, NoL, roughness, metallic, F0, albedo);
	}

	for (int i = 0; i < NUM_SPOT_LIGHTS; ++i)
	{
		if (uboConstant.spotLights[i].enabled == 0)
		{
			continue;
		}

		float distance = length(uboConstant.spotLights[i].position - posWS);

		if (distance > 125)
		{
			// TODO: Define radius on spot lights individually
			continue;
		}

		// Pretend point lights have a radius of 1cm to avoid division by 0
		vec3 pointToLight = normalize(uboConstant.spotLights[i].position - posWS);
		vec3 L = normalize(uboConstant.spotLights[i].direction);
		float attenuation = 1.0 / max((distance * distance), 0.001);
		//attenuation *= max(dot(L, pointToLight), 0.0);
		vec3 radiance = uboConstant.spotLights[i].colour.rgb * attenuation * uboConstant.spotLights[i].brightness;
		float NoL = max(dot(N, pointToLight), 0.0);
		float LoPointToLight = max(dot(L, pointToLight), 0.0);
		float inCone = LoPointToLight - uboConstant.spotLights[i].angle;
		float blendThreshold = 0.1;
		if (inCone < 0.0)
		{
			radiance = vec3(0.0);
		}
		else if (inCone < blendThreshold)
		{
			radiance *= clamp(inCone / blendThreshold, 0.001, 1.0);
		}

		Lo += DoLighting(radiance, N, V, L, NoV, LoPointToLight, roughness, metallic, F0, albedo);
	}

	if (uboConstant.dirLight.enabled != 0)
	{
		vec3 L = normalize(uboConstant.dirLight.direction);
		vec3 radiance = uboConstant.dirLight.colour.rgb * uboConstant.dirLight.brightness;
		float NoL = max(dot(N, L), 0.0);

		float dirLightShadowOpacity = DoShadowMapping(uboConstant.dirLight, uboConstant.shadowSamplingData, posWS, cascadeIndex, shadowMaps, NoL);

		Lo += DoLighting(radiance, N, V, L, NoV, NoL, roughness, metallic, F0, albedo) * dirLightShadowOpacity;
	}

	vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

	vec3 skyColour = mix(uboConstant.skyboxData.colourTop.rgb, uboConstant.skyboxData.colourMid.rgb, 1.0-max(dot(N, vec3(0,1,0)), 0.0));
	skyColour = mix(skyColour, uboConstant.skyboxData.colourBtm.rgb, -min(dot(N, vec3(0,-1,0)), 0.0));

	// Diffse ambient term (IBL)
	vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;	  
    vec3 irradiance = mix(texture(irradianceSampler, N).rgb, skyColour, 0.2);
    vec3 diffuse = irradiance * albedo;

	// Specular ambient term (IBL)
	const float MAX_REFLECTION_LOAD = 5.0;
	vec3 prefilteredColour = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOAD).rgb;
	prefilteredColour += skyColour * 0.1;
	vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
	vec3 specular = prefilteredColour * (F * brdf.x + brdf.y);

	vec3 ambient = (kD * diffuse + specular);

	// TODO: Apply SSAO to ambient term
	vec3 colour = ambient + Lo * pow(ssao, uboConstant.ssaoData.powExp);
	colour = mix(colour, uboConstant.skyboxData.colourFog.rgb, fogDist);

	colour = colour / (colour + vec3(1.0f)); // Reinhard tone-mapping
	colour = pow(colour, vec3(1.0f / 2.2f)); // Gamma correction

	fragColour = vec4(colour, 1.0);

#if 0
	switch (cascadeIndex)
	{
		case 0: fragColour *= vec4(0.85f, 0.4f, 0.3f, 0.0f); return;
		case 1: fragColour *= vec4(0.2f, 1.0f, 1.0f, 0.0f); return;
		case 2: fragColour *= vec4(1.0f, 1.0f, 0.2f, 0.0f); return;
		case 3: fragColour *= vec4(0.4f, 0.8f, 0.2f, 0.0f); return;
		default: fragColour = vec4(1.0f, 0.0f, 1.0f, 0.0f); return;
	}
#endif

	// fragColour = vec4(F, 1);

	// Visualize world pos:
	// fragColour = vec4(posWS*0.1, 1); return;

	// Visualize normals:
	// fragColour = vec4(N*0.5+0.5, 1); return;

	// Visualize screen coords:
	//fragColour = vec4(ex_TexCoord, 0, 1); return;

	// Visualize metallic:
	//fragColour = vec4(metallic, metallic, metallic, 1); return;
}
