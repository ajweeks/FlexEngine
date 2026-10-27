#version 450

#include "vk_misc.glsl"

layout (constant_id = 6) const int ENABLE_WATER_CLIP = 0;

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
	DirectionalLight dirLight;
	PointLight pointLights[NUM_POINT_LIGHTS];
	SpotLight spotLights[NUM_SPOT_LIGHTS];
	AreaLight areaLights[NUM_AREA_LIGHTS];
	SkyboxData skyboxData;
	ShadowSamplingData shadowSamplingData;
	WaterClipData waterClipData;
	float zNear;
	float zFar;
} uboConstant;

layout (location = 0) in vec4 ex_Colour; // rgb: albedo (sRGB), a: marble vein strength (< 0: water)
layout (location = 1) in vec3 ex_NormalWS;
layout (location = 2) in float ex_Depth;
layout (location = 3) in vec3 ex_PositionWS;
layout (location = 4) in vec4 ex_Weights; // Procedural texture weights: grass, layered rock, granular, snow

layout (binding = 1) uniform sampler2D albedoSampler;
layout (binding = 2) uniform sampler2DArrayShadow shadowCascadeSampler;

layout (location = 0) out vec4 fragmentColour;

// Procedural textures are evaluated relative to an origin snapped to a coarse grid near the camera so they keep their
// precision far from the world origin. Frequencies are powers of two so the integer lattice offset is exact.
const float NOISE_ORIGIN_GRID = 2048.0;

float HashI(ivec3 c)
{
	uvec3 u = uvec3(c);
	uint h = u.x * 0x8da6b343u ^ u.y * 0xd8163841u ^ u.z * 0xcb1ab31fu;
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return float(h) * (1.0 / 4294967295.0);
}

// Value noise in [-1, 1] with analytic derivatives (yzw)
vec4 ValueNoiseD(vec3 localScaled, ivec3 latticeOffset)
{
	vec3 p = floor(localScaled);
	vec3 w = localScaled - p;
	ivec3 c = ivec3(p) + latticeOffset;

	vec3 u = w * w * w * (w * (w * 6.0 - 15.0) + 10.0);
	vec3 du = 30.0 * w * w * (w * (w - 2.0) + 1.0);

	float a = HashI(c);
	float b = HashI(c + ivec3(1, 0, 0));
	float cc = HashI(c + ivec3(0, 1, 0));
	float d = HashI(c + ivec3(1, 1, 0));
	float e = HashI(c + ivec3(0, 0, 1));
	float f = HashI(c + ivec3(1, 0, 1));
	float g = HashI(c + ivec3(0, 1, 1));
	float h = HashI(c + ivec3(1, 1, 1));

	float k0 = a;
	float k1 = b - a;
	float k2 = cc - a;
	float k3 = e - a;
	float k4 = a - b - cc + d;
	float k5 = a - cc - e + g;
	float k6 = a - b - e + f;
	float k7 = -a + b + cc - d + e - f - g + h;

	return vec4(-1.0 + 2.0 * (k0 + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x + k7 * u.x * u.y * u.z),
		2.0 * du * vec3(k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z,
						k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x,
						k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y));
}

vec3 g_NoiseLocal;
vec3 g_NoiseOrigin;

// frequency must be a power of two >= 1 / NOISE_ORIGIN_GRID. Derivatives are with respect to world space.
vec4 Noise(float frequency)
{
	vec4 n = ValueNoiseD(g_NoiseLocal * frequency, ivec3(g_NoiseOrigin * frequency));
	n.yzw *= frequency;
	return n;
}

// Same as Noise, with the input offset (to decorrelate layers)
vec4 NoiseOffset(float frequency, vec3 offset)
{
	vec4 n = ValueNoiseD(g_NoiseLocal * frequency + offset, ivec3(g_NoiseOrigin * frequency));
	n.yzw *= frequency;
	return n;
}

void main()
{
	// The view matrix is a rigid transform, so its inverse translation doesn't need a full matrix inverse
	vec3 camPos = -(transpose(mat3(uboConstant.view)) * uboConstant.view[3].xyz);

	vec3 posWS = ex_PositionWS;
	vec3 viewVec = posWS - camPos;
	float dist = length(viewVec);
	vec3 viewDir = viewVec / max(dist, 0.0001);
	vec3 V = -viewDir;

	g_NoiseOrigin = floor(camPos / NOISE_ORIGIN_GRID) * NOISE_ORIGIN_GRID;
	g_NoiseLocal = posWS - g_NoiseOrigin;

	float linDepth = (ex_Depth - uboConstant.zNear) / (uboConstant.zFar - uboConstant.zNear);

	uint cascadeIndex = 0;
	for (uint i = 0; i < NUM_CASCADES; ++i)
	{
		if (linDepth > uboConstant.shadowSamplingData.cascadeDepthSplits[i])
		{
			cascadeIndex = i + 1;
		}
	}

	bool bWater = ex_Colour.a < -0.5;
	// Water isn't drawn inside boats (only in the terrain_water shader: a discard would disable early depth testing)
	if (ENABLE_WATER_CLIP != 0 && bWater && IsInWaterClipVolume(uboConstant.waterClipData, ex_PositionWS))
	{
		discard;
	}

	vec3 N = normalize(ex_NormalWS);
	if (!gl_FrontFacing && bWater)
	{
		N = -N;
	}

	// Fade out high frequency detail with distance to avoid aliasing
	float fineDetail = 1.0 - smoothstep(30.0, 250.0, dist);
	float midDetail = 1.0 - smoothstep(150.0, 1500.0, dist);
	float coarseDetail = 1.0 - smoothstep(1500.0, 8000.0, dist);

	vec3 albedo;
	float roughness = 1.0;
	float specularStrength = 0.0;

	if (bWater)
	{
		albedo = pow(ex_Colour.rgb, vec3(2.2));
		// Gentle ripples
		vec4 ripple0 = Noise(0.5);
		vec4 ripple1 = NoiseOffset(2.0, vec3(17.3, 0.0, 5.1));
		vec3 rippleGradient = (ripple0.yzw * 0.3 + ripple1.yzw * 0.08) * midDetail;
		N = normalize(vec3(-rippleGradient.x, 1.0, -rippleGradient.z));
		roughness = 0.1;
		specularStrength = 1.0;
	}
	else
	{
		vec3 tint = pow(clamp(ex_Colour.rgb, 0.0, 1.0), vec3(2.2));
		// Snow's weight is implied (w holds sky visibility)
		vec4 weights = vec4(max(ex_Weights.xyz, vec3(0.0)), 0.0);
		weights.w = max(1.0 - weights.x - weights.y - weights.z, 0.0);
		weights /= max(weights.x + weights.y + weights.z + weights.w, 0.0001);

		vec3 bump = vec3(0.0);

		// Detail which has faded out entirely isn't evaluated (the results are identical, since it's scaled by zero)
		bool bMidDetail = midDetail > 0.0;
		bool bFineDetail = fineDetail > 0.0;

		// Large scale colour variation, shared by all materials
		vec4 macro = Noise(1.0 / 64.0);
		vec4 macro2 = bMidDetail ? NoiseOffset(1.0 / 8.0, vec3(3.7, 1.3, 9.1)) : vec4(0.0);
		float variation = (macro.x * 0.6 + macro2.x * 0.4 * midDetail) * coarseDetail;

		// Grass: clumpy, with drier (straw coloured) & lusher patches. Kept desaturated, since real grass is
		// much less vivid than it appears.
		vec3 grass = vec3(0.0);
		if (weights.x > 0.001)
		{
			vec4 clumps = bMidDetail ? Noise(2.0) : vec4(0.0);
			vec4 blades = bFineDetail ? NoiseOffset(16.0, vec3(5.5, 2.1, 7.7)) : vec4(0.0);
			float dry = smoothstep(0.0, 0.8, NoiseOffset(1.0 / 32.0, vec3(1.9, 4.4, 0.6)).x);
			float lush = smoothstep(0.2, 0.9, NoiseOffset(1.0 / 16.0, vec3(7.1, 0.3, 2.6)).x);
			grass = tint * mix(vec3(1.0), vec3(1.25, 1.12, 0.78), dry * 0.55);
			grass = mix(grass, tint * vec3(0.85, 0.95, 0.80), lush * (1.0 - dry) * 0.5);
			grass *= 0.88 + 0.2 * variation + (0.12 * clumps.x + 0.1 * blades.x * fineDetail) * midDetail;
			float luminance = dot(grass, vec3(0.2126, 0.7152, 0.0722));
			grass = mix(grass, vec3(luminance), 0.2);
			bump += weights.x * (clumps.yzw * 0.008 + blades.yzw * 0.0015 * fineDetail) * midDetail;
		}

		// Layered rock: strata bands warped by noise, with cracks
		vec3 rock = vec3(0.0);
		if (weights.y > 0.001)
		{
			vec4 warp = bMidDetail ? Noise(1.0 / 16.0) : vec4(0.0);
			float strataCoord = posWS.y * 0.6 + warp.x * 3.0;
			float strata = sin(strataCoord) * 0.5 + 0.5;
			float strata2 = sin(strataCoord * 3.7 + 1.3) * 0.5 + 0.5;
			vec4 grain = bFineDetail ? NoiseOffset(4.0, vec3(2.2, 8.1, 3.3)) : vec4(0.0);
			vec4 chips = bMidDetail ? NoiseOffset(1.0, vec3(9.9, 1.1, 4.2)) : vec4(0.0);
			rock = tint * (0.78 + 0.22 * variation + (0.16 * strata + 0.08 * strata2) * midDetail + 0.1 * grain.x * fineDetail);
			// Strata form subtle ledges in the normal
			vec3 strataBump = vec3(0.0, cos(strataCoord) * 0.6, 0.0);
			bump += weights.y * (chips.yzw * 0.06 + grain.yzw * 0.012 * fineDetail + strataBump * 0.08) * midDetail;
		}

		// Granular (soil, sand, gravel, granite): speckles
		vec3 granular = vec3(0.0);
		if (weights.z > 0.001)
		{
			vec4 speckleNoise = bFineDetail ? NoiseOffset(16.0, vec3(4.1, 6.3, 2.9)) : vec4(0.0);
			vec4 pebbles = bMidDetail ? NoiseOffset(2.0, vec3(8.8, 3.1, 5.6)) : vec4(0.0);
			float speckle = smoothstep(0.55, 0.8, speckleNoise.x) - smoothstep(0.6, 0.85, -speckleNoise.x) * 0.7;
			granular = tint * (0.85 + 0.2 * variation + (0.14 * pebbles.x) * midDetail + 0.22 * speckle * fineDetail);
			bump += weights.z * (pebbles.yzw * 0.015 + speckleNoise.yzw * 0.001 * fineDetail) * midDetail;
		}

		// Snow: bright & soft, with wind ripples
		vec3 snow = vec3(0.0);
		if (weights.w > 0.001)
		{
			vec4 drift = bMidDetail ? NoiseOffset(1.0 / 4.0, vec3(6.6, 2.7, 1.8)) : vec4(0.0);
			snow = tint * (0.92 + 0.08 * drift.x * midDetail);
			bump += weights.w * drift.yzw * 0.08 * midDetail;
			specularStrength += weights.w * 0.25;
		}

		albedo = grass * weights.x + rock * weights.y + granular * weights.z + snow * weights.w;

		// Marble veins
		float vein = clamp(ex_Colour.a, 0.0, 1.0);
		if (vein > 0.01)
		{
			if (bMidDetail)
			{
				vec4 veinWarp = Noise(1.0 / 4.0);
				float veinLine = abs(sin(posWS.x * 0.35 + posWS.y * 0.6 + posWS.z * 0.2 + veinWarp.x * 6.0));
				float veinMask = 1.0 - smoothstep(0.0, 0.12, veinLine);
				albedo = mix(albedo, albedo * vec3(0.45, 0.47, 0.5), veinMask * vein * midDetail);
			}
			specularStrength += vein * 0.3;
		}

		// Remove the component of the bump along the normal & perturb
		bump -= N * dot(bump, N);
		N = normalize(N - bump);
	}

	// [0: deep in a cave, 1: open to the sky]. Caves only receive light from local sources (torches etc.)
	float skyVisibility = bWater ? 1.0 : clamp(ex_Weights.w, 0.0, 1.0);
	float skyLight = skyVisibility * skyVisibility * skyVisibility;

	// Light from local sources, which isn't affected by the sun or sky
	vec3 localLight = vec3(0.0);
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

		localLight += radiance * NoL;
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

		localLight += radiance * NoL;
	}

	vec3 specular = vec3(0.0);
	vec3 groundCol = vec3(0.0);

	float dirLightShadowOpacity = 1.0;
	if (uboConstant.dirLight.enabled != 0)
	{
		vec3 L = normalize(uboConstant.dirLight.direction);
		vec3 radiance = uboConstant.dirLight.colour.rgb * uboConstant.dirLight.brightness;
		float NoL = pow(dot(N, L) * 0.5 + 0.5, 4.0); // Wrapped diffuse

		dirLightShadowOpacity = DoShadowMapping(uboConstant.dirLight, uboConstant.shadowSamplingData, posWS, normalize(ex_NormalWS), linDepth, cascadeIndex, shadowCascadeSampler, max(dot(normalize(ex_NormalWS), L), 0.0));
		// Shadows of the clouds drifting overhead
		dirLightShadowOpacity *= CloudShadow(posWS, camPos, uboConstant.skyboxData);
		// Water scatters light below its surface, so isn't black in shadow
		float sunlight = bWater ? mix(0.35, 1.0, dirLightShadowOpacity) : dirLightShadowOpacity;
		groundCol = albedo * NoL * radiance * sunlight * skyVisibility * skyVisibility;

		if (specularStrength > 0.0)
		{
			vec3 H = normalize(L + V);
			float specPower = mix(16.0, 400.0, 1.0 - roughness);
			specular = radiance * pow(max(dot(N, H), 0.0), specPower) * specularStrength * dirLightShadowOpacity * skyVisibility * max(dot(N, L), 0.0);
		}
	}

	vec3 diffuse = groundCol;
	float fresnel = pow(1.0 - max(dot(N, V), 0.0), 6.0);
	if (!bWater)
	{
		// Slight sheen at grazing angles
		groundCol += (fresnel * 0.3) * groundCol;
	}

	// Light from the sky (its colours are in display space): bluer from overhead, paler from the horizon.
	// Occluded in caves.
	vec3 skyTop = pow(uboConstant.skyboxData.colourTop.rgb, vec3(2.2));
	vec3 skyHorizon = pow(uboConstant.skyboxData.colourMid.rgb, vec3(2.2));
	vec3 skyIrradiance = mix(skyHorizon, skyTop, N.y * 0.5 + 0.5) * 0.9;
	groundCol += albedo * skyIrradiance * skyLight * (0.65 + 0.35 * max(N.y, 0.0));
	groundCol += albedo * localLight;
	groundCol += specular;

	fragmentColour.w = 1.0;
	fragmentColour.xyz = groundCol;
	if (!IsDistanceFogEnabled(uboConstant.skyboxData))
	{
		ApplyFog(linDepth, uboConstant.skyboxData.colourFog.xyz, /* inout */ fragmentColour.xyz);
	}

	fragmentColour.rgb = LinearToDisplay(fragmentColour.rgb, uboConstant.skyboxData.sun.w);

	if (bWater)
	{
		// Reflect the sky (in the same space the skybox is drawn in)
		vec3 R = reflect(viewDir, N);
		R.y = abs(R.y);
		float waterFresnel = 0.03 + 0.97 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
		fragmentColour.rgb = mix(fragmentColour.rgb, SkyColour(R, uboConstant.skyboxData) * mix(0.6, 1.0, dirLightShadowOpacity), waterFresnel);
	}

	ApplyDistanceFog(dist, viewDir, uboConstant.skyboxData, /* inout */ fragmentColour.rgb);
	ApplyCloudLayer(posWS, camPos, uboConstant.skyboxData, /* inout */ fragmentColour.rgb);

	// (sky visibility is shown in place of metalness, which terrain doesn't have)
	DrawDebugOverlay(albedo, N, roughness, skyVisibility, diffuse, specular, vec2(0),
		linDepth, dirLightShadowOpacity, cascadeIndex, 0.0, /* inout */ fragmentColour);
}
