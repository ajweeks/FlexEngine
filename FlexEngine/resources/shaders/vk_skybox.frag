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

uint StarHash(ivec3 c)
{
	uvec3 u = uvec3(c);
	uint h = u.x * 0x8da6b343u ^ u.y * 0xd8163841u ^ u.z * 0xcb1ab31fu;
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return h;
}

float HashToUnit(uint h)
{
	return float(h & 0xFFFFFFu) / float(0x1000000);
}

// One layer of stars: space is divided into cells of a 3D grid, each of which may hold one star, projected onto the
// sphere. Working in 3D avoids the seams & stretching of 2D parameterizations. Brightness follows a steep power law:
// many faint stars & a few bright ones.
vec3 StarLayer(vec3 dir, float cellsPerUnit, float density, float pixelAngle, uint seed)
{
	vec3 p = dir * cellsPerUnit;
	ivec3 baseCell = ivec3(floor(p));
	vec3 result = vec3(0.0);
	// Stars are points, drawn about a pixel across (wider ones would look like discs)
	float radius = pixelAngle * 0.9;
	// Projecting onto the sphere can move a star into a neighboring cell, so check those too
	for (int z = -1; z <= 1; ++z)
	{
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x)
			{
				ivec3 cell = baseCell + ivec3(x, y, z);
				uint h = StarHash(cell + ivec3(int(seed), int(seed) * 7, int(seed) * 13));
				if (HashToUnit(h) > density)
				{
					continue;
				}

				vec3 jitter = vec3(HashToUnit(h * 747796405u + 1u), HashToUnit(h * 2891336453u + 7u), HashToUnit(h * 1181783497u + 3u));
				vec3 cellPos = (vec3(cell) + jitter) / cellsPerUnit;
				float cellRadius = length(cellPos);
				// Only cells the sphere passes through hold stars (each star appears once)
				if (abs(cellRadius - 1.0) > 0.5 / cellsPerUnit)
				{
					continue;
				}
				vec3 starPos = cellPos / cellRadius;
				float angle = acos(clamp(dot(dir, starPos), -1.0, 1.0));
				if (angle > radius * 4.0)
				{
					continue;
				}
				float shape = exp(-(angle * angle) / (radius * radius));

				float magnitude = HashToUnit(h * 3266489917u + 11u);
				float brightness = 0.04 + 1.6 * pow(magnitude, 9.0);

				// Colour from temperature: cool red/orange stars to hot blue ones
				float temperature = HashToUnit(h * 668265263u + 5u);
				vec3 colour = temperature < 0.5 ? mix(vec3(1.0, 0.72, 0.5), vec3(1.0, 0.95, 0.9), temperature * 2.0) :
					mix(vec3(1.0, 0.95, 0.9), vec3(0.7, 0.8, 1.0), (temperature - 0.5) * 2.0);

				// Subtle twinkling
				float twinkle = 0.85 + 0.15 * sin(uboConstant.time.x * (2.0 + 3.0 * jitter.x) + jitter.y * 40.0);
				result += colour * brightness * shape * twinkle;
			}
		}
	}
	return result;
}

vec3 Stars(vec3 dir, float pixelAngle)
{
	// The sky turns about the celestial pole over the day
	float angle = uboConstant.time.y * 6.2831853;
	vec3 axis = normalize(vec3(0.0, 0.8, -0.6));
	float c = cos(angle);
	float s = sin(angle);
	vec3 rotated = dir * c + cross(axis, dir) * s + axis * dot(axis, dir) * (1.0 - c);

	vec3 stars = StarLayer(rotated, 60.0, 0.12, pixelAngle, 0u);
	stars += StarLayer(rotated, 160.0, 0.05, pixelAngle, 17u) * 0.35;

	// The Milky Way: a faint band of light along a great circle
	vec3 galacticPole = normalize(vec3(0.3, 0.4, 0.86));
	float band = exp(-pow(dot(rotated, galacticPole) * 5.0, 2.0));
	float mottling = texture(whiteNoise, rotated.xz * 1.5 + rotated.y).r * 0.5 + 0.5;
	stars += vec3(0.55, 0.6, 0.75) * band * mottling * 0.025;

	// Starlight passes through more air near the horizon
	return stars * smoothstep(-0.02, 0.25, dir.y);
}

vec3 Moon(vec3 dir, SkyboxData skyboxData)
{
	vec3 moonDir = skyboxData.moon.xyz;
	const float moonRadius = 0.014; // Radians (larger than the real moon, so it reads at a glance)
	float cosAngle = dot(dir, moonDir);
	if (cosAngle < cos(moonRadius * 1.5))
	{
		return vec3(0.0);
	}

	vec3 tangent = normalize(cross(moonDir, abs(moonDir.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
	vec3 bitangent = cross(moonDir, tangent);
	vec2 offset = vec2(dot(dir, tangent), dot(dir, bitangent)) / moonRadius;
	float r2 = dot(offset, offset);
	float edge = 1.0 - smoothstep(0.92, 1.0, sqrt(r2));
	if (edge <= 0.0)
	{
		return vec3(0.0);
	}

	// Lit by the sun from wherever it is, which gives the moon its phases
	vec3 normal = normalize(tangent * offset.x + bitangent * offset.y - moonDir * sqrt(max(1.0 - r2, 0.0)));
	float lit = smoothstep(-0.05, 0.15, dot(normal, skyboxData.sun.xyz));
	// Dark maria
	float maria = texture(whiteNoise, offset * 0.12 + vec2(0.31, 0.77)).r * 0.6 + texture(whiteNoise, offset * 0.35 + vec2(0.1, 0.4)).r * 0.4;
	vec3 albedo = vec3(0.92, 0.9, 0.86) * mix(0.62, 1.0, smoothstep(0.35, 0.65, maria));
	// Faint earthshine on the dark side
	return albedo * (lit * 0.95 + 0.03) * edge;
}

void main()
{
	vec3 dir = normalize(ex_TexCoord);
	SkyboxData skyboxData = uboConstant.skyboxData;

	// Noise to prevent banding
	float n = noise(abs(ex_TexCoord.x + ex_TexCoord.y));

	// The camera is near the ground (the sky barely changes over the heights players reach)
	const float altitude = 200.0;
	vec3 radiance = vec3(0.0);
	if (skyboxData.sun.y > -0.3)
	{
		radiance += AtmosphereRadiance(dir, skyboxData.sun.xyz, ATMOS_SUN_INTENSITY, altitude, 16, 6);
	}
	// Moonlight is far dimmer, but is scattered the same way
	if (skyboxData.moon.y > -0.2 && skyboxData.moon.w > 0.0)
	{
		radiance += AtmosphereRadiance(dir, skyboxData.moon.xyz, ATMOS_SUN_INTENSITY * 0.012 * skyboxData.moon.w, altitude, 8, 3);
	}
	vec3 colour = AtmosphereToDisplay(radiance);
	// Airglow & scattered starlight keep the night sky from being pure black
	colour += vec3(0.004, 0.006, 0.012) * (1.0 - smoothstep(-0.1, 0.1, skyboxData.sun.y));

	// Below the horizon, fade towards the ground colour
	colour = mix(colour, skyboxData.colourBtm.rgb, smoothstep(0.0, -0.08, dir.y));

	// Sun disk, dimmed & reddened by the air in front of it
	float sunDisk = smoothstep(0.99955, 0.99975, dot(dir, skyboxData.sun.xyz)) * smoothstep(-0.03, 0.0, dir.y);
	colour = mix(colour, SunColour(skyboxData) * 1.3, sunDisk);

	// Stars & moon are only visible once the sky is dark enough
	float skyLuminance = dot(colour, vec3(0.2126, 0.7152, 0.0722));
	float darkness = (1.0 - smoothstep(0.015, 0.12, skyLuminance)) * skyboxData.sunColour.w;
	if (darkness > 0.0)
	{
		float pixelAngle = max(length(fwidth(dir)), 1e-4);
		colour += Stars(dir, pixelAngle) * darkness;
	}
	vec3 moon = Moon(dir, skyboxData);
	float moonVisibility = (1.0 - smoothstep(0.08, 0.5, skyLuminance) * 0.7) * smoothstep(-0.02, 0.02, dir.y);
	colour = mix(colour, max(colour, moon), clamp(length(moon), 0.0, 1.0) * moonVisibility);

	colour += (n - 0.5) / 255.0;

	// Clouds cover the sun & stars
	vec4 clouds = CloudLayer(dir, 1e30, skyboxData);
	colour = mix(colour, clouds.rgb, clouds.a);

	fragmentColour = vec4(clamp(colour, 0.0, 1.0), 1.0);
}
