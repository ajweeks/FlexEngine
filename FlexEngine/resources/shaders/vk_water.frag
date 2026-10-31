#version 450

#include "vk_misc.glsl"
#include "vk_water_common.glsl"

// Ocean surface (GerstnerWave). The swell's shape comes from the vertices (gerstner waves summed on the CPU), with
// scrolling ripples, light scattered through the crests, foam, a sun glint & the sky's reflection on top.
// The water's body is lit in linear space & tone mapped (LinearToDisplay), the reflection is a display space sky colour.

layout (location = 0) in vec3 ex_PositionWS;
layout (location = 1) in vec2 ex_TexCoord;
layout (location = 2) in vec4 ex_Colour;
layout (location = 3) in mat3 ex_TBN;
layout (location = 6) in float ex_WaveHeight; // Height of the surface above its rest level (m)

layout (location = 0) out vec4 fragColour;

struct OceanData
{
	vec4 top; // Linear: colour of light scattered through thin water (crests)
	vec4 mid;
	vec4 btm; // Linear: colour of deep water
	float fresnelFactor;
	float fresnelPower;
	float skyReflectionFactor;
	float fogFalloff;
	float fogDensity;
};

layout (binding = 0) uniform UBOConstant
{
	vec4 camPos;
	vec4 renderOrigin; // xyz: see Renderer::RenderOriginFor
	mat4 view;
	mat4 viewProjection; // (jittered while TAA is on)
	DirectionalLight dirLight;
	OceanData oceanData;
	SkyboxData skyboxData;
	vec4 time; // X: seconds elapsed since program start, Y: time of day [0,1]
	WaterClipData waterClipData;
} uboConstant;

// Just needed for binding
layout (binding = 1) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

layout (binding = 2) uniform sampler2D normalSampler;

// Tangent space ripple normal (z up)
vec3 RippleNormal(vec2 uv)
{
	return texture(normalSampler, uv).xyz * 2.0 - 1.0;
}

void main()
{
	// Not drawn inside boats
	if (IsInWaterClipVolume(uboConstant.waterClipData, ex_PositionWS))
	{
		discard;
	}

	const float t = uboConstant.time.x;
	// Absolute xz, so ripples don't jump when the render origin moves
	const vec3 posAbs = ex_PositionWS + uboConstant.renderOrigin.xyz;
	vec3 viewVec = ex_PositionWS - uboConstant.camPos.xyz;
	float dist = max(length(viewVec), 0.0001);
	vec3 V = -viewVec / dist;

	// Ripples fade out with distance (where they'd alias), leaving the swell's own normal
	float detail = 1.0 - smoothstep(30.0, 450.0, dist);
	vec3 rippleA = RippleNormal(posAbs.xz * 0.027 + vec2(0.012, 0.007) * t);
	vec3 rippleB = RippleNormal(posAbs.xz * 0.071 + vec2(-0.015, 0.02) * t);
	vec3 rippleC = RippleNormal(posAbs.xz * 0.19 + vec2(0.027, -0.024) * t);
	vec2 ripple = (rippleA.xy * 0.55 + rippleB.xy * 0.35 + rippleC.xy * 0.15 * detail * (QUALITY_LEVEL >= 1 ? 1.0 : 0.0)) * mix(0.15, 1.0, detail);

	vec3 Ng = normalize(ex_TBN[2]);
	vec3 N = normalize(Ng + (normalize(ex_TBN[0]) * ripple.x + normalize(ex_TBN[1]) * ripple.y) * 0.2);
	// Keep normals facing the camera at grazing angles (back facing ones would reflect the ground)
	float NoVRaw = dot(N, V);
	if (NoVRaw < 0.02)
	{
		N = normalize(N + V * (0.02 - NoVRaw));
	}
	float NoV = max(dot(N, V), 0.0);

	vec3 L = normalize(uboConstant.dirLight.direction);
	float sunUp = uboConstant.dirLight.enabled != 0 ? smoothstep(-0.05, 0.08, L.y) : 0.0;
	vec3 sunLight = uboConstant.dirLight.colour * uboConstant.dirLight.brightness * sunUp;
	// Light from the sky, roughly (its colours are display values)
	vec3 ambient = SRGBToLinear(clamp(uboConstant.skyboxData.colourTop.rgb * 0.6 + uboConstant.skyboxData.colourMid.rgb * 0.4, 0.0, 1.0));

	vec3 deepColour = uboConstant.oceanData.btm.rgb;
	vec3 shallowColour = uboConstant.oceanData.top.rgb;

	// Crests: how high the swell is here [0, 1], roughened by the ripples so foam & scattering break up
	float crest = clamp(ex_WaveHeight * 0.18 + 0.5 + (rippleA.x + rippleB.y) * 0.04, 0.0, 1.0);

	// Water body: deep water seen from above, & sunlight scattered through the thin crests towards the viewer (strongest
	// looking towards the sun)
	float NoL = max(dot(Ng, L), 0.0);
	float scatter = WaterCrestScatter(V, L, Ng, crest);
	vec3 body = deepColour * (ambient * 0.9 + sunLight * NoL * 0.35) + shallowColour * sunLight * scatter * 0.6 +
		shallowColour * ambient * crest * 0.1;

	// Foam on the highest crests, in patches drifting with the ripples
	float foamNoise = rippleA.y * 0.5 + rippleB.x * 0.35 + rippleC.y * 0.15 * detail;
	float foam = QUALITY_LEVEL >= 1 ? WaterFoam(crest, 1.0 - Ng.y, foamNoise, detail) : 0.0;
	vec3 foamLit = vec3(0.85, 0.9, 0.92) * (ambient + sunLight * (0.3 + 0.7 * NoL));
	body = mix(body, foamLit, foam);

	vec3 colour = LinearToDisplay(body, uboConstant.skyboxData.sun.w);

	// Sky reflection (display space)
	vec3 R = reflect(-V, N);
	R.y = abs(R.y);
	vec3 reflection = SkyColour(R, uboConstant.skyboxData) * uboConstant.oceanData.skyReflectionFactor;
	float fresnel = WaterFresnel(NoV);
	colour = mix(colour, reflection, fresnel * (1.0 - foam));

	// Sun glint: a tight highlight with a softer sheen around it, sparkling where the ripples catch the sun
	colour += WaterSunGlint(N, V, L, sunUp, uboConstant.skyboxData) * (1.0 - foam);

	fragColour = vec4(colour, 1.0);

	// Fog: the same distance fog & cloud layer as the terrain (in display space, like the colour above)
	if (IsDistanceFogEnabled(uboConstant.skyboxData))
	{
		ApplyDistanceFog(dist, -V, uboConstant.skyboxData, /* inout */ fragColour.rgb);
	}
	else
	{
		vec4 posCS = uboConstant.view * vec4(ex_PositionWS, 1);
		float depthFade = clamp(pow(pow(max(posCS.z, 0.0), uboConstant.oceanData.fogFalloff)*0.0018, uboConstant.oceanData.fogDensity), 0, 1);
		fragColour.rgb = mix(fragColour.rgb, uboConstant.skyboxData.colourMid.xyz, depthFade);
	}
	ApplyCloudLayer(ex_PositionWS, uboConstant.camPos.xyz, uboConstant.skyboxData, /* inout */ fragColour.rgb);
}
