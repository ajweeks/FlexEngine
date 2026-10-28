
// Misc shader types/functions

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define NUM_POINT_LIGHTS 16
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

// Colour pipeline
// - Material colours (const albedo, textures sampled through SRGBToLinear, vertex colours) & lighting are linear.
// - Lit shaders convert their final linear radiance with LinearToDisplay (exposure, tone map, sRGB encoding) and
//   write display values. Lit scene shaders pass the eye adaptation exposure (SkyboxData.sun.w: exactly 1 in daylight,
//   brighter at night), which only scales linear radiance before the tone map. The sky, fog, clouds & unlit shaders (colour, emissive, sprites, text) work in display
//   space directly.
// - Post processing only grades (an identity matrix by default) & the swapchain is UNORM, so display values reach the
//   screen unchanged.
const float DISPLAY_EXPOSURE = 1.0;

float LinearToSRGB(float v)
{
	return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

vec3 LinearToSRGB(vec3 v)
{
	return vec3(LinearToSRGB(v.r), LinearToSRGB(v.g), LinearToSRGB(v.b));
}

float SRGBToLinear(float v)
{
	return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

vec3 SRGBToLinear(vec3 v)
{
	return vec3(SRGBToLinear(v.r), SRGBToLinear(v.g), SRGBToLinear(v.b));
}

// Khronos PBR Neutral tone mapping: leaves colours below ~0.76 untouched (so base colours reproduce faithfully) &
// smoothly compresses highlights towards white
vec3 ToneMapPBRNeutral(vec3 colour)
{
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;

	float x = min(colour.r, min(colour.g, colour.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	colour -= offset;

	float peak = max(colour.r, max(colour.g, colour.b));
	if (peak < startCompression)
	{
		return colour;
	}

	const float d = 1.0 - startCompression;
	float newPeak = 1.0 - d * d / (peak + d - startCompression);
	colour *= newPeak / peak;

	float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
	return mix(colour, vec3(newPeak), g);
}

vec3 LinearToDisplay(vec3 radiance)
{
	return LinearToSRGB(clamp(ToneMapPBRNeutral(max(radiance * DISPLAY_EXPOSURE, vec3(0.0))), 0.0, 1.0));
}

// With eye adaptation: exposure is SkyboxData.sun.w (1 in daylight, higher in the dark, see BaseScene::UpdateExposure).
// Applied to linear radiance before the tone map, like DISPLAY_EXPOSURE.
vec3 LinearToDisplay(vec3 radiance, float exposure)
{
	return LinearToDisplay(radiance * max(exposure, 1.0));
}

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
	// While a cascade is re-rendered for a new light direction, the new map (in the spare layer, cascadeCount) fades in
	// over the old one (see Renderer::UpdateShadowCascades)
	mat4 fadeViewProj;
	vec4 fadeParams; // x: cascade (< 0: none), y: weight of the new map, z: its texel size, w: its depth bias
};

// Point light shadows (see VulkanRenderer::UpdatePointShadowSlots), indexed like the point lights
struct PointShadowData
{
	// [2i]: xyz: render space position light i's shadow faces were rendered from, w: its first array layer (< 0: unshadowed)
	// [2i+1]: x: shadow strength, y: near plane, z: far plane (range)
	vec4 lights[NUM_POINT_LIGHTS * 2];
	// x: tan of each face's half FOV, y: receiver normal offset (texels), z: depth comparison bias, w: face resolution
	vec4 params;
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
	vec4 sun; // xyz: direction towards the sun, w: eye adaptation exposure (>= 1, see LinearToDisplay)
	vec4 clouds; // x: coverage [0, 1] (0: none), y: cloud layer altitude relative to the camera, zw: noise offset (camera xz + wind)
	vec4 moon; // xyz: direction towards the moon, w: brightness [0, 1]
	vec4 sunColour; // rgb: colour of sunlight reaching the ground (display space), w: star visibility [0, 1]
	vec4 weather; // x: cloudiness of the current weather [0, 1], y: regional variation, zw: weather pattern offset
};

// Volumes (e.g. the inside of a boat's hull) in which water surfaces aren't drawn. Each is a stack of ellipses in
// its local space whose semi-axes grow linearly from the bottom to the top. Must match WaterClipData on the CPU.
struct WaterClipVolume
{
	mat4 worldToLocal;
	vec4 extents; // x: half width (top), y: half length (top), z: bottom y, w: top y
	vec4 bottomScale; // x: width scale at the bottom, y: length scale at the bottom
};

struct WaterClipData
{
	WaterClipVolume volumes[4];
	vec4 count; // x: number of volumes in use
};

bool IsInWaterClipVolume(WaterClipData data, vec3 posWS)
{
	int count = int(data.count.x);
	for (int i = 0; i < count; ++i)
	{
		vec3 p = (data.volumes[i].worldToLocal * vec4(posWS, 1.0)).xyz;
		vec4 e = data.volumes[i].extents;
		float t = (p.y - e.z) / max(e.w - e.z, 0.0001);
		if (t >= 0.0 && t <= 1.0)
		{
			vec2 scale = mix(data.volumes[i].bottomScale.xy, vec2(1.0), t);
			vec2 q = p.xz / (e.xy * scale);
			if (dot(q, q) < 1.0)
			{
				return true;
			}
		}
	}
	return false;
}

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

// One shadow map layer's visibility [0: shadowed, 1: lit], or -1 when posWS is outside of it
float SampleShadowLayer(mat4 viewProj, float layer, float texelSize, float depthBias, float normalOffset, vec3 posWS, vec3 N, sampler2DArrayShadow shadowMaps, float NoL)
{
	// Offset receiver along its normal by a few texels to avoid acne without a large depth bias
	// (more offset is needed the more grazing the light is)
	vec3 offsetPosWS = posWS + N * (texelSize * normalOffset * (1.0 - NoL * 0.5));

	vec4 shadowPos = (biasMat * viewProj) * vec4(offsetPosWS, 1.0);
	shadowPos.y = 1.0 - shadowPos.y;
	shadowPos /= shadowPos.w;

	if (shadowPos.z <= 0.0 || shadowPos.z >= 1.0 || any(lessThan(shadowPos.xy, vec2(0.0))) || any(greaterThan(shadowPos.xy, vec2(1.0))))
	{
		return -1.0;
	}

	// Reversed-Z: comparison passes (lit) when receiver is at least as close to the light as the occluder
	float refDepth = shadowPos.z + depthBias;

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
				lit += texture(shadowMaps, vec4(uv, layer, refDepth));
			}
		}
		lit /= 9.0;
	}
	else
	{
		lit = texture(shadowMaps, vec4(shadowPos.xy, layer, refDepth));
	}
	return lit;
}

// A cascade's visibility, including the newer map fading in over it
float SampleShadowCascade(ShadowSamplingData data, uint cascadeIndex, vec3 posWS, vec3 N, sampler2DArrayShadow shadowMaps, float NoL)
{
	float lit = SampleShadowLayer(data.cascadeViewProjMats[cascadeIndex], float(cascadeIndex), data.cascadeTexelSizes[cascadeIndex],
		data.cascadeDepthBiases[cascadeIndex], data.normalOffset, posWS, N, shadowMaps, NoL);
	if (int(data.fadeParams.x) == int(cascadeIndex) && data.fadeParams.x >= 0.0)
	{
		float faded = SampleShadowLayer(data.fadeViewProj, float(data.cascadeCount), data.fadeParams.z, data.fadeParams.w,
			data.normalOffset, posWS, N, shadowMaps, NoL);
		if (faded >= 0.0)
		{
			lit = lit >= 0.0 ? mix(lit, faded, data.fadeParams.y) : faded;
		}
	}
	return lit;
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

	float lit = SampleShadowCascade(shadowSamplingData, cascadeIndex, posWS, N, shadowMaps, NoL);

	// Cross-fade into the next cascade over the last part of this one, so the change in resolution (& in which
	// casters are captured) doesn't show as a line. The next cascade covers all of this one, so it's always valid there.
	if (cascadeIndex + 1u < uint(shadowSamplingData.cascadeCount))
	{
		float cascadeStart = cascadeIndex > 0u ? shadowSamplingData.cascadeDepthSplits[cascadeIndex - 1u] : 0.0;
		float cascadeEnd = shadowSamplingData.cascadeDepthSplits[cascadeIndex];
		float blendStart = mix(cascadeStart, cascadeEnd, 0.8);
		float blend = smoothstep(blendStart, cascadeEnd, linDepth);
		if (blend > 0.0)
		{
			float next = SampleShadowCascade(shadowSamplingData, cascadeIndex + 1u, posWS, N, shadowMaps, NoL);
			if (next >= 0.0)
			{
				lit = lit >= 0.0 ? mix(lit, next, blend) : next;
			}
		}
	}
	lit = lit >= 0.0 ? lit : 1.0;

	// Fade out over the last 10% of the shadow distance to hide the cutoff
	float shadowEnd = shadowSamplingData.cascadeDepthSplits[shadowSamplingData.cascadeCount - 1];
	float fade = smoothstep(shadowEnd * 0.9, shadowEnd, linDepth);
	lit = mix(lit, 1.0, fade);

	return 1.0 - (1.0 - lit) * dirLight.shadowDarkness;
}

// Returns how much of point light lightIndex reaches posWS (render space) [0: shadowed, 1: lit]. A cube map of six
// faces (+x, -x, +y, -y, +z, -z) in consecutive layers of shadowMaps, each a reversed-Z perspective view whose basis
// must match VulkanRenderer::GetPointShadowFaceBasis/GetPointShadowFaceViewProj.
float PointLightShadow(PointShadowData data, int lightIndex, vec3 posWS, vec3 N, sampler2DArrayShadow shadowMaps)
{
	vec4 light = data.lights[lightIndex * 2];
	vec4 info = data.lights[lightIndex * 2 + 1];
	if (light.w < 0.0 || info.x <= 0.0)
	{
		return 1.0;
	}

	float tanHalf = data.params.x;
	float nearPlane = info.y;
	float farPlane = info.z;

	vec3 v = posWS - light.xyz;
	float dist = length(v);
	if (dist >= farPlane)
	{
		return 1.0;
	}

	// Offset the receiver along its normal by about a texel at its distance (acne without a large depth bias)
	float texelSize = 2.0 * tanHalf * max(dist, nearPlane) / data.params.w;
	v += N * (texelSize * data.params.y);

	vec3 av = abs(v);
	int face;
	vec3 F, up;
	if (av.x >= av.y && av.x >= av.z)
	{
		face = v.x >= 0.0 ? 0 : 1;
		F = vec3(sign(v.x), 0.0, 0.0);
		up = vec3(0.0, 1.0, 0.0);
	}
	else if (av.y >= av.z)
	{
		face = v.y >= 0.0 ? 2 : 3;
		F = vec3(0.0, sign(v.y), 0.0);
		up = vec3(0.0, 0.0, 1.0);
	}
	else
	{
		face = v.z >= 0.0 ? 4 : 5;
		F = vec3(0.0, 0.0, sign(v.z));
		up = vec3(0.0, 1.0, 0.0);
	}
	// Like glm::lookAt (left handed)
	vec3 s = normalize(cross(up, F));
	vec3 u = cross(F, s);
	float c = max(dot(v, F), nearPlane);
	vec2 ndc = vec2(dot(v, s), dot(v, u)) / (c * tanHalf);
	vec2 uv = vec2(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y);
	// Reversed-Z perspective depth of the receiver
	float depth = nearPlane / (farPlane - nearPlane) * (farPlane / c - 1.0);
	float refDepth = depth + data.params.z;
	float layer = light.w + float(face);

	float lit;
	if (QUALITY_LEVEL >= 1)
	{
		// 4 bilinear PCF taps (a 3x3 texel footprint)
		float texel = 1.0 / data.params.w;
		lit = texture(shadowMaps, vec4(uv + vec2(-0.5, -0.5) * texel, layer, refDepth));
		lit += texture(shadowMaps, vec4(uv + vec2(0.5, -0.5) * texel, layer, refDepth));
		lit += texture(shadowMaps, vec4(uv + vec2(-0.5, 0.5) * texel, layer, refDepth));
		lit += texture(shadowMaps, vec4(uv + vec2(0.5, 0.5) * texel, layer, refDepth));
		lit *= 0.25;
	}
	else
	{
		lit = texture(shadowMaps, vec4(uv, layer, refDepth));
	}

	// Fade out towards the range so the cutoff isn't visible
	lit = mix(lit, 1.0, smoothstep(farPlane * 0.85, farPlane, dist));
	return mix(1.0, lit, info.x);
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

// Colour of sunlight, reddening as the sun nears the horizon (from the atmosphere model, computed on the CPU)
vec3 SunColour(SkyboxData skyboxData)
{
	return skyboxData.sunColour.rgb;
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

// Matches the sky drawn by the skybox shader (without its noise, sun disk & clouds). The gradient colours are sampled
// from the atmosphere model on the CPU.
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

// Physically based sky: single scattering through an atmosphere of air (Rayleigh) & haze (Mie) with exponential
// density falloff, with a cheap approximation of multiple scattering. Must match BaseScene's CPU version.
const float ATMOS_EARTH_RADIUS = 6360e3;
const float ATMOS_TOP_RADIUS = 6420e3;
const vec3 ATMOS_RAYLEIGH = vec3(5.8e-6, 13.5e-6, 33.1e-6);
const vec3 ATMOS_OZONE = vec3(1.3e-6, 3.76e-6, 0.17e-6); // Absorption only, follows the air's density
const float ATMOS_MIE = 4e-6;
const float ATMOS_RAYLEIGH_HEIGHT = 8000.0;
const float ATMOS_MIE_HEIGHT = 1200.0;
const float ATMOS_MIE_G = 0.8;
const float ATMOS_MULTIPLE_SCATTERING = 0.15;
const float ATMOS_SUN_INTENSITY = 20.0;
// Maps radiance to display values (white balanced so the midday sun is white)
const vec3 ATMOS_WHITE_BALANCE = vec3(1.0, 1.111, 1.3125);
const float ATMOS_EXPOSURE = 1.5;

// Distances to a sphere (centered at the planet's center) from a point at originRadius from the center, along a
// direction whose cosine with the local up direction is mu. Formulated to avoid catastrophic cancellation.
vec2 AtmosRaySphere(float originRadius, float mu, float sphereRadius)
{
	float b = originRadius * mu;
	float c = (originRadius - sphereRadius) * (originRadius + sphereRadius);
	float disc = b * b - c;
	if (disc < 0.0)
	{
		return vec2(-1.0);
	}
	float s = sqrt(disc);
	return vec2(-b - s, -b + s);
}

// Radiance scattered towards the viewer (at the given altitude) along dir, lit from lightDir
vec3 AtmosphereRadiance(vec3 dir, vec3 lightDir, float intensity, float altitude, int sampleCount, int lightSampleCount)
{
	float originRadius = ATMOS_EARTH_RADIUS + max(altitude, 1.0);
	vec2 top = AtmosRaySphere(originRadius, dir.y, ATMOS_TOP_RADIUS);
	float tMax = top.y;
	if (tMax <= 0.0)
	{
		return vec3(0.0);
	}
	vec2 ground = AtmosRaySphere(originRadius, dir.y, ATMOS_EARTH_RADIUS);
	if (ground.x > 0.0)
	{
		tMax = ground.x;
	}

	float mu = dot(dir, lightDir);
	float phaseR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
	float g2 = ATMOS_MIE_G * ATMOS_MIE_G;
	float phaseM = 3.0 / (8.0 * PI) * ((1.0 - g2) * (1.0 + mu * mu)) / ((2.0 + g2) * pow(1.0 + g2 - 2.0 * ATMOS_MIE_G * mu, 1.5));

	float segment = tMax / float(sampleCount);
	float opticalDepthR = 0.0;
	float opticalDepthM = 0.0;
	vec3 sumR = vec3(0.0);
	vec3 sumM = vec3(0.0);
	vec3 origin = vec3(0.0, originRadius, 0.0);
	for (int i = 0; i < sampleCount; ++i)
	{
		vec3 p = origin + dir * (segment * (float(i) + 0.5));
		float r = length(p);
		float h = r - ATMOS_EARTH_RADIUS;
		float densityR = exp(-h / ATMOS_RAYLEIGH_HEIGHT) * segment;
		float densityM = exp(-h / ATMOS_MIE_HEIGHT) * segment;
		opticalDepthR += densityR;
		opticalDepthM += densityM;

		float lightMu = dot(p, lightDir) / r;
		vec2 lightGround = AtmosRaySphere(r, lightMu, ATMOS_EARTH_RADIUS);
		if (lightGround.x > 0.0)
		{
			// In the planet's shadow
			continue;
		}
		float lightSegment = AtmosRaySphere(r, lightMu, ATMOS_TOP_RADIUS).y / float(lightSampleCount);
		float lightDepthR = 0.0;
		float lightDepthM = 0.0;
		for (int j = 0; j < lightSampleCount; ++j)
		{
			float lh = length(p + lightDir * (lightSegment * (float(j) + 0.5))) - ATMOS_EARTH_RADIUS;
			lightDepthR += exp(-lh / ATMOS_RAYLEIGH_HEIGHT) * lightSegment;
			lightDepthM += exp(-lh / ATMOS_MIE_HEIGHT) * lightSegment;
		}
		vec3 tau = (ATMOS_RAYLEIGH + ATMOS_OZONE) * (opticalDepthR + lightDepthR) + ATMOS_MIE * 1.11 * (opticalDepthM + lightDepthM);
		vec3 attenuation = exp(-tau);
		sumR += attenuation * densityR;
		sumM += attenuation * densityM;
	}

	return intensity * (sumR * ATMOS_RAYLEIGH * (phaseR + ATMOS_MULTIPLE_SCATTERING) + sumM * ATMOS_MIE * phaseM);
}

vec3 AtmosphereToDisplay(vec3 radiance)
{
	// Same display transform as lit surfaces (must match atmosphere::ToDisplay in BaseScene.cpp)
	return LinearToDisplay(radiance * ATMOS_WHITE_BALANCE * ATMOS_EXPOSURE);
}

// Clouds: a layer of procedural noise at a fixed altitude, drifting with the wind. Shared by the skybox (which
// draws them), lighting (where they cast shadows) & geometry they're in front of, so they all match.

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

// Coverage at a point in the cloud layer: the scene's maximum, scaled by the current weather, which also varies
// from region to region (so some areas are clear while others are overcast)
float CloudCoverage(vec2 p, float coverage, SkyboxData skyboxData)
{
	vec2 q = (p + skyboxData.weather.zw) / 40000.0;
	float regional = CloudFbm(q + vec2(41.3, 7.7), 3) * 2.0 - 1.0;
	float weather = clamp(skyboxData.weather.x + regional * skyboxData.weather.y, 0.0, 1.0);
	return coverage * weather;
}

// [0, 1] cloud opacity at a point in the cloud layer (noise space: world xz + wind offset)
float CloudDensity(vec2 p, float coverage, int octaves, SkyboxData skyboxData)
{
	float localCoverage = CloudCoverage(p, coverage, skyboxData);
	if (localCoverage <= 0.02)
	{
		return 0.0;
	}
	vec2 q = p / 2400.0;
	// Warp the domain so clouds billow rather than looking like plain noise
	vec2 warp = vec2(CloudValueNoise(q * 0.7 + vec2(3.7, 1.3)), CloudValueNoise(q * 0.7 + vec2(8.2, 5.9))) - 0.5;
	float n = CloudFbm(q + warp * 0.6, octaves);
	float threshold = 1.0 - localCoverage;
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
	float density = CloudDensity(cloudPos, coverage, 4, skyboxData);
	return 1.0 - density * 0.8;
}

// Colour (rgb, display space) & opacity (a) of the cloud layer where the view ray along dir crosses it, if that's
// within maxDistance of the camera
vec4 CloudLayer(vec3 dir, float maxDistance, SkyboxData skyboxData)
{
	float coverage = skyboxData.clouds.x;
	float relativeAltitude = skyboxData.clouds.y;
	// Below the layer only clouds above are visible, & vice versa (when flying high)
	if (coverage <= 0.0 || abs(dir.y) < 0.0005 || relativeAltitude / dir.y <= 0.0)
	{
		return vec4(0.0);
	}

	float t = relativeAltitude / dir.y;
	if (t > maxDistance)
	{
		return vec4(0.0);
	}
	vec2 cloudPos = dir.xz * t + skyboxData.clouds.zw;
	float density = CloudDensity(cloudPos, coverage, 5, skyboxData);
	if (density <= 0.001)
	{
		return vec4(0.0);
	}

	// Self shadowing: denser cloud towards the sun blocks more light
	vec2 sunStep = normalize(skyboxData.sun.xz + vec2(1e-4)) * 350.0;
	float towardsSun = CloudDensity(cloudPos + sunStep, coverage, 3, skyboxData);
	float lit = mix(1.0, 0.35, towardsSun * 0.8 + density * 0.2);

	vec3 sunColour = SunColour(skyboxData);
	float daylight = smoothstep(-0.1, 0.1, skyboxData.sun.y);
	// Forward scattering brightens cloud edges around the sun (silver lining)
	float cosSun = max(dot(dir, skyboxData.sun.xyz), 0.0);
	float silverLining = pow(cosSun, 10.0) * (1.0 - density) * 1.5;
	vec3 ambient = mix(skyboxData.colourMid.rgb, skyboxData.colourTop.rgb, 0.5);
	vec3 colour = ambient * 0.7 + sunColour * (lit * 0.75 + silverLining) * daylight;
	// Moonlit at night
	float moonlight = skyboxData.moon.w * smoothstep(-0.05, 0.15, skyboxData.moon.y) * (1.0 - daylight);
	colour += vec3(0.05, 0.06, 0.08) * lit * moonlight;
	// Viewed from above, sunlit tops are brighter
	if (relativeAltitude < 0.0)
	{
		colour = mix(colour, sunColour * daylight + ambient * 0.3, 0.4);
	}

	// Fade out towards the horizon, where the layer is very far away (& blends into the haze)
	float fade = exp(-t / 30000.0) * smoothstep(0.0, 0.06, abs(dir.y));
	return vec4(clamp(colour, 0.0, 1.0), density * fade);
}

// Clouds in front of geometry (e.g. covering the land below when looking down from a mountain above the clouds, or
// mountain tops which reach into them). Applied to display space colours, like distance fog.
void ApplyCloudLayer(vec3 posWS, vec3 camPos, SkyboxData skyboxData, inout vec3 fragmentColour)
{
	vec3 viewVec = posWS - camPos;
	float dist = length(viewVec);
	vec4 clouds = CloudLayer(viewVec / max(dist, 0.0001), dist, skyboxData);
	fragmentColour = mix(fragmentColour, clouds.rgb, clouds.a);
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
