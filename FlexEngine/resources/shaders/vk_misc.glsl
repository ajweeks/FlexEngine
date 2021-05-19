
// Misc shader types/functions

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define NUM_POINT_LIGHTS 8
#define NUM_SPOT_LIGHTS 8
#define NUM_AREA_LIGHTS 8

// Specialization constants
layout (constant_id = 0) const int SSAO_KERNEL_SIZE = 64;
layout (constant_id = 1) const int TAA_SAMPLE_COUNT = 2;
layout (constant_id = 2) const int QUALITY_LEVEL = 1;
layout (constant_id = 3) const int NUM_CASCADES = 4;
layout (constant_id = 4) const int DEBUG_OVERLAY_INDEX = 0;
layout (constant_id = 5) const int ENABLE_FOG = 1;

const float PI = 3.14159265359;

const mat4 biasMat = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 
);

// Types
struct DirectionalLight 
{
	vec3 direction;
	int enabled;
	vec3 colour;
	float brightness;
	int castShadows;
	float shadowDarkness;
	vec2 _pad;
};

struct PointLight 
{
	vec3 position;
	int enabled;
	vec3 colour;
	float brightness;
};

struct SpotLight 
{
	vec3 position;
	int enabled;
	vec3 colour;
	float brightness;
	vec3 direction;
	float angle;
};

struct AreaLight 
{
	vec3 colour;
	float brightness;
	vec3 _pad;
	int enabled;
	vec4 points[4];
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

struct SkyboxData
{
	vec4 colourTop;
	vec4 colourMid;
	vec4 colourBtm;
	vec4 colourFog;
};

// Functions
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

vec3 ReconstructVSPosFromDepth(mat4 invProj, vec2 uv, float depth)
{
	float x = uv.x * 2.0f - 1.0f;
	float y = (1.0f - uv.y) * 2.0f - 1.0f;
	vec4 pos = vec4(x, y, depth, 1.0f);
	vec4 posVS = invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return posNDC;
}

vec3 ReconstructWSPosFromDepth(mat4 invProj, mat4 invView, vec2 uv, float depth)
{
	float x = uv.x * 2.0f - 1.0f;
	float y = (1.0f - uv.y) * 2.0f - 1.0f;
	vec4 pos = vec4(x, y, depth, 1.0f);
	vec4 posVS = invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return (invView * vec4(posNDC, 1)).xyz;
}

float DoShadowMapping(DirectionalLight dirLight, ShadowSamplingData shadowSamplingData, vec3 posWS, uint cascadeIndex, sampler2DArray shadowMaps, float NoL)
{
	float dirLightShadowOpacity = 1.0;
	if (dirLight.castShadows != 0)
	{
		vec4 transformedShadowPos = (biasMat * shadowSamplingData.cascadeViewProjMats[cascadeIndex]) * vec4(posWS, 1.0);
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
				float shadowSampleContrib = dirLight.shadowDarkness / ((sampleRadius*2 + 1) * (sampleRadius*2 + 1));

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
					dirLightShadowOpacity = 1.0 - dirLight.shadowDarkness;
				}
			}
		}
	}
	return dirLightShadowOpacity;
}

vec3 ColourByShadowCascade(uint cascadeIndex)
{
	switch (cascadeIndex)
	{
		case 0:  return vec3(0.8f, 0.4f, 0.3f);
		case 1:  return vec3(0.2f, 1.0f, 1.0f);
		case 2:  return vec3(1.0f, 1.0f, 0.2f);
		case 3:  return vec3(0.4f, 0.8f, 0.2f);
		default: return vec3(1.0f, 0.0f, 1.0f);
	}
}

void DrawDebugOverlay(vec3 albedo, vec3 N, float roughness, float metallic,
    vec3 diffuse, vec3 specular, vec2 texCoord, float linDepth,
    float shadow, uint cascadeIndex, float ssao, inout vec4 fragColour)
{
    switch (DEBUG_OVERLAY_INDEX)
    {
    case 1: // Albedo
        fragColour = vec4(albedo.xyz, 1.0);
        break;
    case 2: // Normal
        fragColour = vec4(N.xyz * 0.5 + 0.5, 1.0);
        break;
    case 3: // Roughness
        fragColour = vec4(roughness.xxx, 1.0);
        break;
    case 4: // Metallic
        fragColour = vec4(metallic.xxx, 1.0);
        break;
    case 5: // Diffuse lighting
        fragColour = vec4(diffuse.xyz, 1.0);
        break;
    case 6: // Specular lighting
        fragColour = vec4(specular.xyz, 1.0);
        break;
    case 7: // Tex coords
        fragColour = vec4(texCoord.xy, 0.0, 1.0);
        break;
    case 8: // Linear depth
        fragColour = vec4(linDepth.xxx, 1.0);
        break;
    case 9: // Shadow
        fragColour = vec4(shadow.xxx, 1.0);
        break;
    case 10: // Shadow cascade
        fragColour.xyz = ColourByShadowCascade(cascadeIndex) * fragColour.xyz;
        break;
    case 11: // SSAO
        fragColour = vec4(ssao.xxx, 1.0);
        break;
    }
}

void ApplyFog(float linearDepth, vec3 colourFog, inout vec3 fragmentColour)
{
	if (ENABLE_FOG == 1)
	{
		float dist = clamp(linearDepth * 0.075 - 0.01, 0.0, 1.0);
		dist = smoothstep(0.0, 0.5, dist);

		fragmentColour = mix(fragmentColour, colourFog, dist);
	}
}
