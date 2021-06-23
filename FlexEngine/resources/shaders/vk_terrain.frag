#version 450

#include "vk_misc.glsl"
#include "vk_noises.glsl"

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
	float zNear;
	float zFar;
} uboConstant;

layout (location = 0) in vec2 ex_TexCoord;
layout (location = 1) in vec4 ex_Colour;
layout (location = 2) in vec3 ex_NormalWS;
layout (location = 3) in vec3 ex_PositionWS;
layout (location = 4) in float ex_Depth;

layout (binding = 2) uniform sampler2D albedoSampler;
layout (binding = 3) uniform sampler2DArray shadowCascadeSampler;

layout (location = 0) out vec4 fragmentColour;

vec3 pal(float t, vec3 a, vec3 b, vec3 c, vec3 d)
{
    return a + b * cos(6.28318 * (c * t + d));
}

vec3 palette(float v)
{
	
	//return pal(v.x, vec3(0.8,0.5,0.4), vec3(0.2,0.4,0.2), vec3(2.0,1.0,1.0), vec3(0.0,0.25,0.25));
	return pal(v.x, vec3(0.5,0.5,0.5), vec3(0.5,0.5,0.5), vec3(1.0,1.0,1.0), vec3(0.0,0.33,0.67));
}

void main()
{
	vec3 albedo = texture(albedoSampler, ex_TexCoord).rgb;
	float height = ex_Colour.x;
	// Nudge up to account for float precision
	float blendWeight = ex_Colour.y;
	int matID0 = int(ex_Colour.z + 0.5);
	int matID1 = int(ex_Colour.w + 0.5);
	vec3 N = normalize(ex_NormalWS);
	float roughness = 1.0;
	float metallic = 0.0;
	float ssao = 0.0;

	mat4 invView = inverse(uboConstant.view);
	vec3 camPos = vec3(invView[3][0], invView[3][1], invView[3][2]);

	float linDepth = (ex_Depth - uboConstant.zNear) / (uboConstant.zFar - uboConstant.zNear);

	vec3 V = normalize(camPos.xyz - ex_PositionWS);

	float fresnel = pow(1.0 - dot(N, V), 6.0);

	uint cascadeIndex = 0;
	for (uint i = 0; i < NUM_CASCADES; ++i)
	{
		if (linDepth > uboConstant.shadowSamplingData.cascadeDepthSplits[i])
		{
			cascadeIndex = i + 1;
		}
	}

	int lodLevel = int(cascadeIndex);

	float darkening = 1.0;

	if (lodLevel <= 2)
	{
		// Rock-face/clif pattern
		vec3 bump = fbmd_7(ex_PositionWS * vec3(3.0, 0.05, 3.0)).yzw;
		float bumpInfluence = 0.4 * (1.0-abs(N.y));
		bumpInfluence *= fbm_4(ex_PositionWS * 0.005) * 0.8 + 0.2;
		// Blend in bump map where surfaces are not pointing up
		N = normalize(N + bumpInfluence * bump);

		//float stretchedRock = 1.0-VoronoiColumns((10.0 - ex_PositionWS.x) + ex_PositionWS.xz * 0.9, 0.965, nearestCell, nearestCellPos);

		// if (height < 0.51)
		{
			// Dried cracks pattern
			bumpInfluence = 1.3 * (1.0 - pow(max(abs(N.y) - 1.0, 0.0), 10.0));
			bumpInfluence *= clamp((-fbm_4(ex_PositionWS * 0.009)), 0.0, 1.0) * 0.8 + 0.2;
			vec2 nearestCell, nearestCellPos;
			float sharpness = 0.965;
			vec2 _x;
			float dist = VoronoiDistance(ex_PositionWS.xz * 0.9, nearestCell, nearestCellPos, _x);

			// float maxNormalDist = 0.05;
			float maxNormalDist = 0.15;
			if (linDepth < maxNormalDist)
			{
				float blendDist = linDepth / maxNormalDist;
				bumpInfluence *= (1.0 - blendDist*blendDist);

				vec2 dirToEdge = normalize(nearestCellPos);
				float theta = atan(dirToEdge.y, dirToEdge.x);
				// float crackWidth = 1.0-(1.0-blendDist)*(1.0-blendDist) + 0.5;
				// Technically this should take into account FOV, since it's attempting
				// to match reduction in detail as objects get further from camera.
				float crackWidth = min(abs(dFdx(ex_TexCoord.x)), abs(dFdx(ex_TexCoord.y))) * 1000.0; // blendDist
				float crackDepth = 1.0;

				float crackMask = 1.0 - smoothstep(0.0, 1.0 - clamp(crackWidth, 0.96, 1.0), dist);
				vec3 perterbedN = normalize(mix(vec3(0, 1, 0), -vec3(cos(theta), 0.0, sin(theta)), vec3(clamp(crackMask * crackDepth, 0.0, 1.0))));
				N = normalize(N + bumpInfluence * perterbedN);
			}
			// abs(val) + abs(val + 1) prevents identical hashes along axes (where some value is 0)
			darkening = 0.8 + 0.3 * (hash1(abs(nearestCell) + abs(nearestCell + 1)) - 0.5);
		}
	}
	else
	{
		// TODO: Darken/increase roughness of surface here to account for missing cracks
	}

	//fragmentColour.xy = abs(dFdx(ex_TexCoord)) * 100.0; fragmentColour.zw = 0.0.xx; return;

	vec3 groundCol;

	float minHeight = 0.0;
	float maxHeight = 1.0;

	vec3 lowCols[] = {
		vec3(0.61, 0.19, 0.029), // Orange
		vec3(0.065, 0.04, 0.02), // Dirt
		vec3(0.09, 0.03, 0.02), // Red
		vec3(0.07, 0.08, 0.1), // Blue
	};
	vec3 highCols[] = {
		vec3(0.82, 0.48, 0.20), // Beige
		vec3(0.045, 0.06, 0.02), // Grass
		vec3(0.75, 0.76, 0.80), // White
		vec3(0.08, 0.09, 0.21), // Dark blue
	};


	// Value going from 0 at the start of the blend range, to 0.5 at the edge of a cell
	// float blendWeight2 = float((uint(ex_Colour.y) & 0xFFFF)) / 65536.0;
	// float blendWeight0 = clamp(1.0 - blendWeight1 - blendWeight2, 0.0, 1.0);

	// {
	 	int m0 = matID0 % 4;
	 	int m1 = matID1 % 4;
	// 	int m2 = matID2 % 4;

		float alpha = clamp((height - minHeight) / (maxHeight - minHeight), 0.0, 1.0);
	 	vec3 c0 = mix(lowCols[m0], highCols[m0], alpha);
	 	vec3 c1 = mix(lowCols[m1], highCols[m1], alpha);
	// 	vec3 c2 = mix(lowCols[m2], highCols[m2], alpha);
	// 	groundCol = c0;// * blendWeight0 + c1 * blendWeight1 + c2 * blendWeight2;
	// 	groundCol = pow(groundCol, vec3(2.2));

	// 	// groundCol = vec3(float(m0) / 4.0, float(matID1) / 4.0, float(matID2) / 4.0);
	// }

	groundCol = c0;// mix(c0, c1, blendWeight);
	groundCol = pow(groundCol, vec3(2.2));

	// groundCol = (float(matID2 + 0.5) / 4.0).xxx; fragmentColour.xyz = groundCol; fragmentColour.w = 1.0;
	// fragmentColour.rgb *= 1.0-(pow(blendWeight1*2.0, 200.0));
	// return;
	// groundCol.rg = vec2(blendWeight1, blendWeight2);
	// groundCol.b = 0.0;

	for (int i = 0; i < NUM_POINT_LIGHTS; ++i)
	{
		if (uboConstant.pointLights[i].enabled == 0)
		{
			continue;
		}

		float distance = length(uboConstant.pointLights[i].position - ex_PositionWS);

		if (distance > 125)
		{
			// TODO: Define radius on point lights individually
			continue;
		}

		// Pretend point lights have a radius of 1cm to avoid division by 0
		float attenuation = 1.0 / max((distance * distance), 0.001);
		vec3 L = normalize(uboConstant.pointLights[i].position - ex_PositionWS);
		vec3 radiance = uboConstant.pointLights[i].colour.rgb * attenuation * uboConstant.pointLights[i].brightness;
		float NoL = max(dot(N, L), 0.0);

		groundCol += groundCol * radiance * NoL;
	}

	for (int i = 0; i < NUM_SPOT_LIGHTS; ++i)
	{
		if (uboConstant.spotLights[i].enabled == 0)
		{
			continue;
		}

		float distance = length(uboConstant.spotLights[i].position - ex_PositionWS);

		if (distance > 125)
		{
			// TODO: Define radius on spot lights individually
			continue;
		}

		// Pretend point lights have a radius of 1cm to avoid division by 0
		vec3 pointToLight = normalize(uboConstant.spotLights[i].position - ex_PositionWS);
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

		groundCol += groundCol * radiance * NoL;
	}

	float light = 0.0;// = dot(N, L) * 0.5 + 0.5;

	float dirLightShadowOpacity = 1.0;
	if (uboConstant.dirLight.enabled != 0)
	{
		vec3 L = normalize(uboConstant.dirLight.direction);
		vec3 radiance = uboConstant.dirLight.colour.rgb * uboConstant.dirLight.brightness;
		float NoL = pow(dot(N, L) * 0.5 + 0.5, 4.0); // Wrapped diffuse

		dirLightShadowOpacity = DoShadowMapping(uboConstant.dirLight, uboConstant.shadowSamplingData, ex_PositionWS, cascadeIndex, shadowCascadeSampler, NoL);
		light = (0.75 * dirLightShadowOpacity + 0.25);
		groundCol *= NoL * radiance;
	}

	groundCol *= darkening;

	groundCol *= light;
	vec3 diffuse = groundCol;
	vec3 specular = vec3(0);
	groundCol += (fresnel * 1.1) * groundCol;
	groundCol += (2.0 * max(dot(N, vec3(0, 1, 0)), 0.0)) * uboConstant.skyboxData.colourTop.rgb * groundCol;

	fragmentColour.w = 1.0;
	fragmentColour.xyz = groundCol;
	ApplyFog(linDepth, uboConstant.skyboxData.colourFog.xyz, /* inout */ fragmentColour.xyz);

	fragmentColour.rgb = fragmentColour.rgb / (fragmentColour.rgb + vec3(1.0)); // Reinhard tone-mapping
	fragmentColour.rgb = pow(fragmentColour.rgb, vec3(1.0 / 2.2f)); // Gamma correction

	// Colourize by chunk ID & neighbor chunk ID
	// fragmentColour.rgb = (palette(ex_Colour.z + 0.5) + palette(ex_Colour.w)) * 0.5;

	// Display chunk borders
	// fragmentColour.rgb *= 1.0-(pow(blendWeight*2.0, 200.0));

	// fragmentColour.rgb = ex_NormalWS;

    DrawDebugOverlay(albedo, N, roughness, metallic, diffuse, specular, ex_TexCoord,
     linDepth, dirLightShadowOpacity, cascadeIndex, ssao, /* inout */ fragmentColour);
}
