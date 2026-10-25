#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "vk_misc.glsl"

layout (location = 0) in vec3 ex_TexCoord;

layout (location = 0) out vec4 fragmentColour;

layout (binding = 0) uniform UBOConstant
{
	SkyboxData skyboxData;
	vec4 time; // X: seconds elapsed since program start, Y: time of day [0,1]
} uboConstant;

layout (binding = 1) uniform sampler2D whiteNoise;
layout (binding = 2) uniform samplerCube cubemap;

float noise(float s)
{
	return fract(546354.846451 * s * s + 4564.4123);
}

// Returns the clouds' colour (rgb) & opacity (a) in the given view direction
vec4 SampleClouds(vec3 dir, SkyboxData skyboxData)
{
	float coverage = skyboxData.clouds.x;
	float relativeAltitude = skyboxData.clouds.y;
	// Below the layer only clouds above are visible, & vice versa (when flying high)
	if (coverage <= 0.0 || abs(dir.y) < 0.0005 || relativeAltitude / dir.y <= 0.0)
	{
		return vec4(0.0);
	}

	float t = relativeAltitude / dir.y;
	vec2 cloudPos = dir.xz * t + skyboxData.clouds.zw;
	float density = CloudDensity(cloudPos, coverage, 5);
	if (density <= 0.001)
	{
		return vec4(0.0);
	}

	// Self shadowing: denser cloud towards the sun blocks more light
	vec2 sunStep = normalize(skyboxData.sun.xz + vec2(1e-4)) * 350.0;
	float towardsSun = CloudDensity(cloudPos + sunStep, coverage, 3);
	float lit = mix(1.0, 0.35, towardsSun * 0.8 + density * 0.2);

	vec3 sunColour = SunColour(skyboxData);
	float daylight = smoothstep(-0.15, 0.15, skyboxData.sun.y);
	// Forward scattering brightens cloud edges around the sun (silver lining)
	float cosSun = max(dot(dir, skyboxData.sun.xyz), 0.0);
	float silverLining = pow(cosSun, 10.0) * (1.0 - density) * 1.5;
	vec3 ambient = mix(skyboxData.colourMid.rgb, skyboxData.colourTop.rgb, 0.5);
	vec3 colour = ambient * 0.7 + sunColour * (lit * 0.75 + silverLining) * daylight;
	// Viewed from above, sunlit tops are brighter
	if (relativeAltitude < 0.0)
	{
		colour = mix(colour, sunColour * daylight + ambient * 0.3, 0.4);
	}

	// Fade out towards the horizon, where the layer is very far away (& blends into the haze)
	float fade = exp(-t / 30000.0) * smoothstep(0.0, 0.06, abs(dir.y));
	return vec4(clamp(colour, 0.0, 1.0), density * fade);
}

void main()
{
	vec3 dir = normalize(ex_TexCoord);
	SkyboxData skyboxData = uboConstant.skyboxData;

	// Noise to prevent banding
	float n = noise(abs(ex_TexCoord.x + ex_TexCoord.y));

	vec2 uv = vec2(abs(dir.x), abs(dir.z)) * 2.0;
	if (dir.y < 0.25 && dir.y > -0.25)
	{
		uv = vec2(abs(dir.x), abs(dir.y)) * 2.0;
	}
	float n_w = texture(whiteNoise, uv).r;

	float h = sign(dir.y) * pow(abs(dir.y), 0.3) + n * 0.03;

	float tw = 1.0-max(h, 0.0);
	float bw = max(-h, 0.0);

	float w0 = tw;
	float w1 = bw;
	float w2 = h > 0 ? 0.0 : 1.0;

	vec3 colour = mix(
		mix(skyboxData.colourTop.xyz, skyboxData.colourMid.xyz, w0),
		mix(skyboxData.colourMid.xyz, skyboxData.colourBtm.xyz, w1), w2);

	// Sun disk & the light scattered around it
	colour += SunGlow(dir, skyboxData);
	float sunDisk = smoothstep(0.99955, 0.99975, dot(dir, skyboxData.sun.xyz)) * smoothstep(-0.03, 0.0, dir.y);
	colour = mix(colour, SunColour(skyboxData) * 1.3, sunDisk);

	float timeOfDay = uboConstant.time.y;
	float nightStart = 0.36;
	float nightEnd = 0.63;
	if (timeOfDay > nightStart && timeOfDay < nightEnd)
	{
		float nightProgress = (timeOfDay - nightStart) / (nightEnd - nightStart);
		float nightProgressParabola = 1.0-(nightProgress-0.5)*(nightProgress-0.5)*4.0;
		nightProgressParabola = pow(nightProgressParabola, 0.25);
		float starThreshold = mix(-100.0, -12.0, nightProgressParabola);
		colour += 0.9 * exp(starThreshold * n_w);
	}

	// Clouds cover the sun & stars
	vec4 clouds = SampleClouds(dir, skyboxData);
	colour = mix(colour, clouds.rgb, clouds.a);

	fragmentColour = vec4(clamp(colour, 0.0, 1.0), 1.0);
}
