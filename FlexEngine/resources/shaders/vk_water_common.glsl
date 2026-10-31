// Water surface shading shared by the ocean (vk_water) & terrain lakes & sea (vk_terrain). Include after vk_misc.glsl.

// Wind driven waves (summed gerstner-like directional waves) for surfaces whose geometry is flat. Returns the surface
// normal & (in outCrest) how high the waves are here [0, 1]. Positions are split into the render origin (a 2km grid)
// & a local offset so the phases stay precise far from the world origin. strength scales the slopes; detail [0, 1]
// fades out the short waves (where they'd alias). Fewer waves at lower shader quality levels.
vec3 WaterWaveNormal(vec2 originXZ, vec2 localXZ, float t, float strength, float detail, out float outCrest)
{
	// x, y: direction, z: wavelength (m), w: steepness
	const vec4 waves[8] = vec4[8](
		vec4(0.94, 0.34, 23.0, 0.22),
		vec4(0.62, -0.78, 14.0, 0.2),
		vec4(-0.31, 0.95, 9.0, 0.18),
		vec4(0.99, -0.12, 5.3, 0.16),
		vec4(0.45, 0.89, 3.1, 0.14),
		vec4(-0.83, 0.56, 1.9, 0.12),
		vec4(0.17, -0.98, 1.1, 0.1),
		vec4(0.71, 0.70, 0.7, 0.08));
	int count = QUALITY_LEVEL <= 0 ? 4 : (QUALITY_LEVEL == 1 ? 6 : 8);

	vec2 slope = vec2(0.0);
	float height = 0.0;
	float totalWeight = 0.0;
	for (int i = 0; i < count; ++i)
	{
		vec2 dir = waves[i].xy;
		float wavelength = waves[i].z;
		// Short waves fade out first
		float fade = i < 3 ? 1.0 : detail * detail;
		if (fade <= 0.0)
		{
			continue;
		}
		float k = 6.2831853 / wavelength;
		// Deep water dispersion: w = sqrt(g k)
		float omega = sqrt(9.81 * k);
		// (the origin's part of the phase is reduced to [0, 1) cycles before it's combined with the rest)
		float cycles = fract(dot(dir, originXZ) / wavelength);
		float phase = 6.2831853 * cycles + k * dot(dir, localXZ) - omega * t;
		// Gerstner waves have sharp crests & wide troughs: approximated by shaping the sine's slope & height
		float s = sin(phase);
		float c = cos(phase);
		float sharp = 0.5 + 0.5 * s;
		float steepness = waves[i].w * fade;
		slope += dir * (c * steepness * (0.6 + 0.8 * sharp));
		height += (sharp * sharp) * steepness;
		totalWeight += waves[i].w;
	}
	outCrest = totalWeight > 0.0 ? clamp(height / (totalWeight * 0.55), 0.0, 1.0) : 0.0;
	slope *= strength;
	return normalize(vec3(-slope.x, 1.0, -slope.y));
}

// Fresnel reflectance of water (Schlick, F0 = 0.02)
float WaterFresnel(float NoV)
{
	return 0.02 + 0.98 * pow(1.0 - clamp(NoV, 0.0, 1.0), 5.0);
}

// Sunlight scattered through thin wave crests towards the viewer (strongest looking towards the sun). Linear, multiply
// by the water's shallow colour & the sun's radiance.
float WaterCrestScatter(vec3 V, vec3 L, vec3 N, float crest)
{
	float towardsSun = pow(clamp(dot(V, -L) * 0.5 + 0.5, 0.0, 1.0), 4.0);
	return (towardsSun * 0.8 + 0.15 * max(dot(N, L), 0.0)) * crest * crest;
}

// Foam [0, 1] on the steep parts of high crests, broken up by noise ([-1, 1])
float WaterFoam(float crest, float steepness, float noise, float detail)
{
	float steep = smoothstep(0.03, 0.12, steepness);
	return smoothstep(0.8, 0.97, crest + noise * 0.25) * steep * mix(0.3, 0.75, detail);
}

// Display space sun glint: a tight highlight with a softer sheen around it, sparkling where the ripples catch the sun.
// sunUp fades it out as the sun sets.
vec3 WaterSunGlint(vec3 N, vec3 V, vec3 L, float sunUp, SkyboxData skyboxData)
{
	vec3 H = normalize(V + L);
	float NoH = max(dot(N, H), 0.0);
	float glint = pow(NoH, 1200.0) * 6.0 + pow(NoH, 140.0) * 0.35;
	return SunColour(skyboxData) * glint * sunUp;
}
