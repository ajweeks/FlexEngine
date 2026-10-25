
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
	vec4 cascadeDepthSplits; // Normalized linear depth at which each cascade ends
	vec4 cascadeTexelSizes; // World-space size of one shadow map texel
	vec4 cascadeDepthBiases; // In normalized light-space depth
	int cascadeCount;
	float normalOffset; // In texels
	vec2 _pad;
};

struct SSAOSamplingData
{
	int enabled; // TODO: Make specialization constant
	float powExp;
	float ambientIntensity;
	float ambientShadowAmount;
};

struct SkyboxData
{
	vec4 colourTop;
	vec4 colourMid;
	vec4 colourBtm;
	vec4 colourFog; // w: distance fog distance (<= 1: disabled)
	vec4 sun; // xyz: direction towards the sun
	vec4 clouds; // x: coverage [0, 1] (0: none), y: cloud layer altitude relative to the camera, zw: noise offset (camera xz + wind)
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

// Returns 1.0 when fully lit, (1 - shadowDarkness) when fully shadowed
// shadowMaps must be bound with a depth-comparison sampler (hardware PCF)
float DoShadowMapping(DirectionalLight dirLight, ShadowSamplingData shadowSamplingData, vec3 posWS, vec3 N, float linDepth, uint cascadeIndex, sampler2DArrayShadow shadowMaps, float NoL)
{
	if (dirLight.castShadows == 0 || cascadeIndex >= uint(shadowSamplingData.cascadeCount))
	{
		// Beyond shadow distance
		return 1.0;
	}

	// Offset receiver along its normal by a few texels to avoid acne without a large depth bias
	// (more offset is needed the more grazing the light is)
	float texelSize = shadowSamplingData.cascadeTexelSizes[cascadeIndex];
	vec3 offsetPosWS = posWS + N * (texelSize * shadowSamplingData.normalOffset * (1.0 - NoL * 0.5));

	vec4 shadowPos = (biasMat * shadowSamplingData.cascadeViewProjMats[cascadeIndex]) * vec4(offsetPosWS, 1.0);
	shadowPos.y = 1.0 - shadowPos.y;
	shadowPos /= shadowPos.w;

	if (shadowPos.z <= 0.0 || shadowPos.z >= 1.0)
	{
		return 1.0;
	}

	// Reversed-Z: comparison passes (lit) when receiver is at least as close to the light as the occluder
	float refDepth = shadowPos.z + shadowSamplingData.cascadeDepthBiases[cascadeIndex];

	float lit;
	if (QUALITY_LEVEL >= 1)
	{
		// 3x3 bilinear-filtered taps (each tap is itself a 2x2 hardware PCF), covering a 4x4 texel footprint
		vec2 shadowMapTexelSize = 1.0 / vec2(textureSize(shadowMaps, 0).xy);
		lit = 0.0;
		for (int x = -1; x <= 1; ++x)
		{
			for (int y = -1; y <= 1; ++y)
			{
				vec2 uv = shadowPos.xy + vec2(x, y) * shadowMapTexelSize;
				lit += texture(shadowMaps, vec4(uv, float(cascadeIndex), refDepth));
			}
		}
		lit /= 9.0;
	}
	else
	{
		lit = texture(shadowMaps, vec4(shadowPos.xy, float(cascadeIndex), refDepth));
	}

	// Fade out over the last 10% of the shadow distance to hide the cutoff
	float shadowEnd = shadowSamplingData.cascadeDepthSplits[shadowSamplingData.cascadeCount - 1];
	float fade = smoothstep(shadowEnd * 0.9, shadowEnd, linDepth);
	lit = mix(lit, 1.0, fade);

	return 1.0 - (1.0 - lit) * dirLight.shadowDarkness;
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
   	case 0:
   		return;
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
		dist = smoothstep(0.0, 0.8, dist);

		fragmentColour = mix(fragmentColour, colourFog, dist);
	}
}

// Colour of sunlight, reddening as the sun nears the horizon
vec3 SunColour(SkyboxData skyboxData)
{
	return mix(vec3(1.0, 0.5, 0.25), vec3(1.0, 0.96, 0.88), smoothstep(-0.02, 0.35, skyboxData.sun.y));
}

// Light scattered around the sun (in the sky's final, display space colour). Haze near the horizon scatters more.
vec3 SunGlow(vec3 dir, SkyboxData skyboxData)
{
	float cosSun = max(dot(dir, skyboxData.sun.xyz), 0.0);
	float daylight = smoothstep(-0.12, 0.05, skyboxData.sun.y);
	float horizon = 1.0 - smoothstep(0.0, 0.5, abs(dir.y));
	float glow = pow(cosSun, 600.0) * 0.6 + pow(cosSun, 24.0) * (0.12 + 0.25 * horizon) + pow(cosSun, 3.0) * 0.06 * horizon;
	return SunColour(skyboxData) * glow * daylight;
}

// Matches the sky drawn by the skybox shader (without its noise, sun disk & clouds)
vec3 SkyColour(vec3 dir, SkyboxData skyboxData)
{
	float h = sign(dir.y) * pow(abs(dir.y), 0.3);
	float w0 = 1.0 - max(h, 0.0);
	float w1 = max(-h, 0.0);
	float w2 = h > 0.0 ? 0.0 : 1.0;
	vec3 gradient = mix(
		mix(skyboxData.colourTop.xyz, skyboxData.colourMid.xyz, w0),
		mix(skyboxData.colourMid.xyz, skyboxData.colourBtm.xyz, w1), w2);
	return clamp(gradient + SunGlow(dir, skyboxData), 0.0, 1.0);
}

// Clouds: a layer of procedural noise at a fixed altitude, drifting with the wind. Shared by the skybox (which
// draws them) & lighting (where they cast shadows), so shadows match the visible clouds.

float CloudHash(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float CloudValueNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	float a = CloudHash(i);
	float b = CloudHash(i + vec2(1.0, 0.0));
	float c = CloudHash(i + vec2(0.0, 1.0));
	float d = CloudHash(i + vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float CloudFbm(vec2 p, int octaves)
{
	float sum = 0.0;
	float amplitude = 0.5;
	float total = 0.0;
	const mat2 rotation = mat2(0.8, 0.6, -0.6, 0.8);
	for (int i = 0; i < octaves; ++i)
	{
		sum += amplitude * CloudValueNoise(p);
		total += amplitude;
		p = rotation * p * 2.03 + vec2(17.1, 9.3);
		amplitude *= 0.5;
	}
	return sum / total;
}

// [0, 1] cloud opacity at a point in the cloud layer (noise space: world xz + wind offset)
float CloudDensity(vec2 p, float coverage, int octaves)
{
	vec2 q = p / 2400.0;
	// Warp the domain so clouds billow rather than looking like plain noise
	vec2 warp = vec2(CloudValueNoise(q * 0.7 + vec2(3.7, 1.3)), CloudValueNoise(q * 0.7 + vec2(8.2, 5.9))) - 0.5;
	float n = CloudFbm(q + warp * 0.6, octaves);
	float threshold = 1.0 - coverage;
	return smoothstep(threshold - 0.08, threshold + 0.22, n);
}

// Fraction of sunlight reaching a point through the cloud layer
float CloudShadow(vec3 posWS, vec3 camPos, SkyboxData skyboxData)
{
	float coverage = skyboxData.clouds.x;
	if (coverage <= 0.0 || skyboxData.sun.y <= 0.02)
	{
		return 1.0;
	}

	float cloudAltitude = skyboxData.clouds.y + camPos.y;
	if (posWS.y >= cloudAltitude)
	{
		return 1.0;
	}

	// Follow the sun's direction up to the cloud layer
	float t = (cloudAltitude - posWS.y) / skyboxData.sun.y;
	vec2 cloudPos = posWS.xz + skyboxData.sun.xz * t + (skyboxData.clouds.zw - camPos.xz);
	float density = CloudDensity(cloudPos, coverage, 4);
	return 1.0 - density * 0.75;
}

// Distance fog, enabled when skyboxData.colourFog.w (the distance at which the scene is fully obscured) is > 1.
// Blends towards the sky's colour in the view direction so distant geometry dissolves into the sky. Must be applied
// to the final (tone mapped & gamma corrected) colour to match the skybox.
bool IsDistanceFogEnabled(SkyboxData skyboxData)
{
	return ENABLE_FOG == 1 && skyboxData.colourFog.w > 1.0;
}

void ApplyDistanceFog(float dist, vec3 viewDir, SkyboxData skyboxData, inout vec3 fragmentColour)
{
	if (IsDistanceFogEnabled(skyboxData))
	{
		float t = dist / skyboxData.colourFog.w;
		float amount = max(1.0 - exp(-3.0 * t * t * t), smoothstep(0.85, 1.05, t));
		// Lower the horizon slightly so fogged terrain matches the sky just above it
		vec3 skyDir = normalize(vec3(viewDir.x, max(viewDir.y, 0.0), viewDir.z));
		fragmentColour = mix(fragmentColour, SkyColour(skyDir, skyboxData), clamp(amount, 0.0, 1.0));
	}
}
