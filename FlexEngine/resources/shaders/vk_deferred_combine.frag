#version 450

// Deferred PBR Combine

#define NUM_POINT_LIGHTS 8

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (constant_id = 2) const int QUALITY_LEVEL = 1;
layout (constant_id = 3) const int NUM_CASCADES = 4;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 fragColor;

const float PI = 3.14159265359;

struct DirectionalLight 
{
	vec3 direction;
	int enabled;
	vec3 color;
	float brightness;
	int castShadows;
	float shadowDarkness;
	vec2 _pad;
};

struct PointLight 
{
	vec3 position;
	int enabled;
	vec3 color;
	float brightness;
};

struct ShadowSamplingData
{
	mat4 cascadeViewProjMats[NUM_CASCADES];
	vec4 cascadeDepthSplits;
};

struct SSAOSamplingData
{
	int enabled; // TODO: Make specialization constant
	float powExp;
	vec2 _pad;
};

layout (binding = 0) uniform UBOConstant
{
	vec4 camPos;
	mat4 invView;
	mat4 invProj;
	DirectionalLight dirLight;
	PointLight pointLights[NUM_POINT_LIGHTS];
	ShadowSamplingData shadowSamplingData;
	SSAOSamplingData ssaoData;
	float zNear;
	float zFar;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	int enableIrradianceSampler;
} uboDynamic;

layout (binding = 2) uniform sampler2D brdfLUT;
layout (binding = 3) uniform samplerCube irradianceSampler;
layout (binding = 4) uniform samplerCube prefilterMap;
layout (binding = 5) uniform sampler2D depthBuffer;
layout (binding = 6) uniform sampler2D ssaoBuffer;
layout (binding = 7) uniform sampler2DArray shadowMaps;

layout (binding = 8) uniform sampler2D normalRoughnessTex;
layout (binding = 9) uniform sampler2D albedoMetallicTex;

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float NdotH2 = NdotH * NdotH;

	float nom = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float nom = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

vec3 DoLighting(vec3 radiance, vec3 N, vec3 V, vec3 L, float NoV, float NoL,
	float roughness, float metallic, vec3 F0, vec3 albedo)
{
	vec3 H = normalize(V + L);

	// Cook-Torrance BRDF
	float NDF = DistributionGGX(N, H, roughness);
	float G = GeometrySmith(N, V, L, roughness);
	vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

	vec3 kS = F;
	vec3 kD = vec3(1.0) - kS;
	kD *= 1.0 - metallic; // Pure metals have no diffuse lighting

	vec3 nominator = NDF * G * F;
	float denominator = 4 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001; // Add epsilon to prevent divide by zero
	vec3 specular = nominator / denominator;

	return (kD * albedo / PI + specular) * radiance * NoL;
}

vec3 ReconstructVSPosFromDepth(vec2 uv, float depth)
{
	float x = uv.x * 2.0f - 1.0f;
	float y = (1.0f - uv.y) * 2.0f - 1.0f;
	vec4 pos = vec4(x, y, depth, 1.0f);
	vec4 posVS = uboConstant.invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return posNDC;
}

vec3 ReconstructWSPosFromDepth(vec2 uv, float depth)
{
	float x = uv.x * 2.0f - 1.0f;
	float y = (1.0f - uv.y) * 2.0f - 1.0f;
	vec4 pos = vec4(x, y, depth, 1.0f);
	vec4 posVS = uboConstant.invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return (uboConstant.invView * vec4(posNDC, 1)).xyz;
}

const mat4 biasMat = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 
);

void main()
{
    vec3 N = texture(normalRoughnessTex, ex_TexCoord).rgb;
    N = mat3(uboConstant.invView) * N; // view space -> world space
    float roughness = texture(normalRoughnessTex, ex_TexCoord).a;

    float depth = texture(depthBuffer, ex_TexCoord).r;
    vec3 viewPos = ReconstructVSPosFromDepth(ex_TexCoord, depth);
    vec3 worldPos = ReconstructWSPosFromDepth(ex_TexCoord, depth);
    float depthN = viewPos.z*(1/48.0);
	
    float invDist = 1.0f/(uboConstant.zFar-uboConstant.zNear);

	float linDepth = (viewPos.z-uboConstant.zNear)*invDist;
	// fragColor = vec4(vec3(linDepth), 1); return;

    vec3 albedo = texture(albedoMetallicTex, ex_TexCoord).rgb;	
    float metallic = texture(albedoMetallicTex, ex_TexCoord).a;

    float ssao = (uboConstant.ssaoData.enabled == 1 ? texture(ssaoBuffer, ex_TexCoord).r : 1.0f);

	// fragColor = vec4(vec3(pow(ssao, uboConstant.ssaoPowExp)), 1); return;

	uint cascadeIndex = 0;
	for (uint i = 0; i < NUM_CASCADES; ++i)
	{
		if (linDepth > uboConstant.shadowSamplingData.cascadeDepthSplits[i])
		{
			cascadeIndex = i + 1;
		}
	}

	vec3 V = normalize(uboConstant.camPos.xyz - worldPos);
	vec3 R = reflect(-V, N);

	float NoV = max(dot(N, V), 0.0);

	// If diaelectric, F0 should be 0.04, if metal it should be the albedo color
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

		float distance = length(uboConstant.pointLights[i].position - worldPos);

		if (distance > 125)
		{
			// TODO: Define radius on point lights individually
			continue;
		}

		// Pretend point lights have a radius of 1cm to avoid division by 0
		float attenuation = 1.0 / max((distance * distance), 0.001);
		vec3 L = normalize(uboConstant.pointLights[i].position - worldPos);
		vec3 radiance = uboConstant.pointLights[i].color.rgb * attenuation * uboConstant.pointLights[i].brightness;
		float NoL = max(dot(N, L), 0.0);

		Lo += DoLighting(radiance, N, V, L, NoV, NoL, roughness, metallic, F0, albedo);
	}

	if (uboConstant.dirLight.enabled != 0)
	{
		vec3 L = normalize(uboConstant.dirLight.direction);
		vec3 radiance = uboConstant.dirLight.color.rgb * uboConstant.dirLight.brightness;
		float NoL = max(dot(N, L), 0.0);

		float dirLightShadowOpacity = 1.0;
		if (uboConstant.dirLight.castShadows != 0)
		{
			vec4 transformedShadowPos = (biasMat * uboConstant.shadowSamplingData.cascadeViewProjMats[cascadeIndex]) * vec4(worldPos, 1.0);
			transformedShadowPos.y = 1.0f - transformedShadowPos.y;
			transformedShadowPos /= transformedShadowPos.w;
			
			if (transformedShadowPos.z > -1.0 && transformedShadowPos.z < 1.0)
			{
				float baseBias = 0.0005;
				float bias = max(baseBias * (1.0 - NoL), baseBias * 0.01);

				if (QUALITY_LEVEL >= 1)
				{
					int sampleRadius = 3;
					float spread = 1.75;
					float shadowSampleContrib = uboConstant.dirLight.shadowDarkness / ((sampleRadius*2 + 1) * (sampleRadius*2 + 1));
					vec3 shadowMapTexelSize = 1.0 / textureSize(shadowMaps, 0);

					for (int x = -sampleRadius; x <= sampleRadius; ++x)
					{
						for (int y = -sampleRadius; y <= sampleRadius; ++y)
						{
							float shadowDepth = texture(shadowMaps, vec3(transformedShadowPos.xy + vec2(x, y) * shadowMapTexelSize.xy*spread, cascadeIndex)).r;

							if (shadowDepth > transformedShadowPos.z + bias)
							{
								dirLightShadowOpacity -= shadowSampleContrib;
							}
						}
					}
				}
				else
				{
					float shadowDepth = texture(shadowMaps, vec3(transformedShadowPos.xy, cascadeIndex)).r;
					if (shadowDepth > transformedShadowPos.z + bias)
					{
						dirLightShadowOpacity = 1.0 - uboConstant.dirLight.shadowDarkness;
					}
				}
			}
		}

		Lo += DoLighting(radiance, N, V, L, NoV, NoL, roughness, metallic, F0, albedo) * dirLightShadowOpacity;
	}

	vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

	vec3 ambient;
	if (uboDynamic.enableIrradianceSampler != 0)
	{
		// Diffse ambient term (IBL)
		vec3 kS = F;
	    vec3 kD = 1.0 - kS;
	    kD *= 1.0 - metallic;	  
	    vec3 irradiance = texture(irradianceSampler, N).rgb;
	    vec3 diffuse = irradiance * albedo;

		// Specular ambient term (IBL)
		const float MAX_REFLECTION_LOAD = 5.0;
		vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOAD).rgb;
		vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
		vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

	    ambient = (kD * diffuse + specular);
	}
	else
	{
		ambient = vec3(0.03) * albedo;
	}

	// TODO: Apply SSAO to ambient term
	vec3 color = ambient + Lo * pow(ssao, uboConstant.ssaoData.powExp);

	color = color / (color + vec3(1.0f)); // Reinhard tone-mapping
	color = pow(color, vec3(1.0f / 2.2f)); // Gamma correction

	fragColor = vec4(color, 1.0);

#if 0
	switch (cascadeIndex)
	{
		case 0: fragColor *= vec4(0.85f, 0.4f, 0.3f, 0.0f); return;
		case 1: fragColor *= vec4(0.2f, 1.0f, 1.0f, 0.0f); return;
		case 2: fragColor *= vec4(1.0f, 1.0f, 0.2f, 0.0f); return;
		case 3: fragColor *= vec4(0.4f, 0.8f, 0.2f, 0.0f); return;
		default: fragColor = vec4(1.0f, 0.0f, 1.0f, 0.0f); return;
	}
#endif

	// fragColor = vec4(F, 1);

	// Visualize world pos:
	// fragColor = vec4(worldPos*0.1, 1); return;

	// Visualize normals:
	// fragColor = vec4(N*0.5+0.5, 1); return;

	// Visualize screen coords:
	//fragColor = vec4(ex_TexCoord, 0, 1); return;

	// Visualize metallic:
	//fragColor = vec4(metallic, metallic, metallic, 1); return;
}
