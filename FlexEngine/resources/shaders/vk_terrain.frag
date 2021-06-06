#version 450

#include "vk_misc.glsl"

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
layout (location = 1) in flat vec4 ex_Colour; // Can't interpolate due to float packing
layout (location = 2) in vec3 ex_NormalWS;
layout (location = 3) in vec3 ex_PositionWS;
layout (location = 4) in float ex_Depth;

layout (binding = 2) uniform sampler2D albedoSampler;
layout (binding = 3) uniform sampler2DArray shadowMaps;

layout (location = 0) out vec4 fragmentColour;

float hash1(vec2 p)
{
    p = 50.0 * fract(p * 0.3183099);
    return fract(p.x * p.y * (p.x + p.y));
}

float hash1(float n)
{
    return fract(n * 17.0 * fract(n * 0.3183099));
}

vec2 hash2(float n)
{
	return fract(sin(vec2(n, n + 1.0)) * vec2(43758.5453123,22578.1459123));
}

vec2 hash2(vec2 p) 
{
    const vec2 k = vec2(0.3183099, 0.3678794);
    p = p * k + k.yx;
    return fract(16.0 * k * fract(p.x * p.y * (p.x + p.y)));
}

// Noises from https://www.shadertoy.com/view/4ttSWf

// Value noise, and its analytical derivatives
vec4 noised(vec3 x)
{
    vec3 p = floor(x);
    vec3 w = fract(x);
    vec3 u = w * w * w * (w * (w * 6.0 - 15.0) + 10.0);
    vec3 du = 30.0 * w * w * (w * (w - 2.0) + 1.0);

    float n = p.x + 317.0 * p.y + 157.0 * p.z;
    
    float a = hash1(n + 0.0);
    float b = hash1(n + 1.0);
    float c = hash1(n + 317.0);
    float d = hash1(n + 318.0);
    float e = hash1(n + 157.0);
	float f = hash1(n + 158.0);
    float g = hash1(n + 474.0);
    float h = hash1(n + 475.0);

    float k0 =  a;
    float k1 =  b - a;
    float k2 =  c - a;
    float k3 =  e - a;
    float k4 =  a - b - c + d;
    float k5 =  a - c - e + g;
    float k6 =  a - b - e + f;
    float k7 = -a + b + c - d + e - f - g + h;

    return vec4( -1.0 + 2.0 * (k0 + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x + k7 * u.x * u.y * u.z), 
                      2.0 *  du * vec3(k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z,
                                      k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x,
                                      k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y));
}

float noise(vec3 x)
{
    vec3 p = floor(x);
    vec3 w = fract(x);
    
    vec3 u = w * w * w * (w * (w * 6.0 - 15.0) + 10.0);

    float n = p.x + 317.0 * p.y + 157.0 * p.z;
    
    float a = hash1(n + 0.0);
    float b = hash1(n + 1.0);
    float c = hash1(n + 317.0);
    float d = hash1(n + 318.0);
    float e = hash1(n + 157.0);
	float f = hash1(n + 158.0);
    float g = hash1(n + 474.0);
    float h = hash1(n + 475.0);

    float k0 =  a;
    float k1 =  b - a;
    float k2 =  c - a;
    float k3 =  e - a;
    float k4 =  a - b - c + d;
    float k5 =  a - c - e + g;
    float k6 =  a - b - e + f;
    float k7 = -a + b + c - d + e - f - g + h;

    return -1.0 + 2.0 * (k0 + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x + k7 * u.x * u.y * u.z);
}

vec3 noised(vec2 x)
{
    vec2 p = floor(x);
    vec2 w = fract(x);
    vec2 u = w * w * w * (w * (w * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * w * w * (w * (w - 2.0) + 1.0);
    
    float a = hash1(p + vec2(0, 0));
    float b = hash1(p + vec2(1, 0));
    float c = hash1(p + vec2(0, 1));
    float d = hash1(p + vec2(1, 1));

    float k0 = a;
    float k1 = b - a;
    float k2 = c - a;
    float k4 = a - b - c + d;

    return vec3(-1.0 + 2.0 * (k0 + k1 * u.x + k2 * u.y + k4 * u.x * u.y), 
                       2.0 * du * vec2(k1 + k4 * u.y, k2 + k4 * u.x));
}

float noise(vec2 x)
{
    vec2 p = floor(x);
    vec2 w = fract(x);
    vec2 u = w * w * w * (w * (w * 6.0 - 15.0) + 10.0);

    float a = hash1(p + vec2(0, 0));
    float b = hash1(p + vec2(1, 0));
    float c = hash1(p + vec2(0, 1));
    float d = hash1(p + vec2(1, 1));
    
    return -1.0 + 2.0 * (a + (b - a) * u.x + (c - a) * u.y + (a - b - c + d) * u.x * u.y);
}

// https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
uint pcg_hash(uint n)
{
    uint state = n * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

vec2 Hash2(vec2 p)
{
	const vec2 k = vec2(0.3183099, 0.3678794);
	p = p * k + vec2(k.y, k.x);
	return fract(16.0 * k * fract(p.x * p.y * (p.x + p.y)));
}

// FBM

const mat3 m3  = mat3( 0.00,  0.80,  0.60,
                      -0.80,  0.36, -0.48,
                      -0.60, -0.48,  0.64);
const mat3 m3i = mat3( 0.00, -0.80, -0.60,
                       0.80,  0.36, -0.48,
                       0.60, -0.48,  0.64);
const mat2 m2 = mat2(  0.80,  0.60,
                      -0.60,  0.80);
const mat2 m2i = mat2( 0.80, -0.60,
                       0.60,  0.80);

//------------------------------------------------------------------------------------------

float fbm_4(vec2 x)
{
    float f = 1.9;
    float s = 0.55;
    float a = 0.0;
    float b = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        float n = noise(x);
        a += b * n;
        b *= s;
        x = f * m2 * x;
    }
	return a;
}

float fbm_4(vec3 x)
{
    float f = 2.0;
    float s = 0.5;
    float a = 0.0;
    float b = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        float n = noise(x);
        a += b * n;
        b *= s;
        x = f * m3 * x;
    }
	return a;
}

vec4 fbmd_7(vec3 x)
{
    float f = 1.92;
    float s = 0.5;
    float a = 0.0;
    float b = 0.5;
    vec3  d = vec3(0.0);
    mat3  m = mat3(1.0,0.0,0.0,
                   0.0,1.0,0.0,
                   0.0,0.0,1.0);
    for (int i = 0; i < 7; ++i)
    {
        vec4 n = noised(x);
        a += b * n.x; // accumulate values		
        d += b * m * n.yzw;// accumulate derivatives
        b *= s;
        x = f * m3 * x;
        m = f * m3i * m;
    }
	return vec4(a, d);
}

vec4 fbmd_8(vec3 x)
{
    float f = 2.0;
    float s = 0.65;
    float a = 0.0;
    float b = 0.5;
    vec3  d = vec3(0.0);
    mat3  m = mat3(1.0, 0.0, 0.0,
                   0.0, 1.0, 0.0,
                   0.0, 0.0, 1.0);
    for (int i = 0; i < 8; ++i)
    {
        vec4 n = noised(x);
        a += b * n.x;// accumulate values		
        if (i < 4)
        {
	        d += b * m * n.yzw; // accumulate derivatives
	    }
        b *= s;
        x = f * m3 * x;
        m = f * m3i * m;
    }
	return vec4(a, d);
}

float fbm_9(vec2 x)
{
    float f = 1.9;
    float s = 0.55;
    float a = 0.0;
    float b = 0.5;
    for (int i = 0; i < 9; ++i)
    {
        float n = noise(x);
        a += b * n;
        b *= s;
        x = f * m2 * x;
    }
	return a;
}

vec3 fbmd_9(vec2 x)
{
    float f = 1.9;
    float s = 0.55;
    float a = 0.0;
    float b = 0.5;
    vec2  d = vec2(0.0);
    mat2  m = mat2(1.0, 0.0, 0.0, 1.0);
    for (int i = 0; i < 9; ++i)
    {
        vec3 n = noised(x);
        
        a += b * n.x; // accumulate values		
        d += b * m * n.yz; // accumulate derivatives
        b *= s;
        x = f * m2 * x;
        m = f * m2i * m;
    }
	return vec3(a, d);
}

float VoronoiDistance(vec2 pos, out vec2 outNearestCell, out vec2 outNearestCellPos)
{
	vec2 posCell = floor(pos);
	vec2 posCellF = fract(pos);

	// Regular Voronoi: find cell of nearest point
	vec2 nearestCell;
	vec2 nearestCellPos;

	float closestEdge = 8.0;
	for (int j = -1; j <= 1; j++)
	{
		for (int i = -1; i <= 1; i++)
		{
			vec2 cellIndex = vec2(i, j);
			vec2 cellRandomPoint = Hash2(posCell + cellIndex);
			vec2 delta = cellIndex + cellRandomPoint - posCellF;
			float distSq = dot(delta, delta);

			if (distSq < closestEdge)
			{
				closestEdge = distSq;
				nearestCellPos = delta;
				nearestCell = cellIndex;
			}
		}
	}

	// Find distance to nearest edge in any neighboring cell
	closestEdge = 8.0;
	for (int j = -2; j <= 2; j++)
	{
		for (int i = -2; i <= 2; i++)
		{
			vec2 cellIndex = nearestCell + vec2(i, j);
			vec2 cellRandomPoint = Hash2(posCell + cellIndex);
			vec2 delta = cellIndex + cellRandomPoint - posCellF;

			vec2 diff = delta - nearestCellPos;
			float diffLenSq = dot(diff, diff);
			if (diffLenSq > 0.00001)
			{
				closestEdge = min(closestEdge, dot(0.5 * (nearestCellPos + delta), diff / diffLenSq));
			}
		}
	}

	outNearestCell = posCell + nearestCell;
	outNearestCellPos = nearestCellPos;
	
	// Square distance to the closest edge in the voronoi diagram
	return closestEdge;
}

float VoronoiColumns(vec2 pos, float sharpness, out vec2 outNearestCell, out vec2 outNearestCellPos)
{
	float dist = VoronoiDistance(pos, outNearestCell, outNearestCellPos);
	return smoothstep(0.0, 1.0 - clamp(sharpness, 0.0, 1.0), dist);
}

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
	int matID0 = int((uint(ex_Colour.z) >> 16));
	int matID1 = int((uint(ex_Colour.z) & 0xFFFF));
	int matID2 = int((uint(ex_Colour.w) & 0xFFFF));
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
			float dist = VoronoiDistance(ex_PositionWS.xz * 0.9, nearestCell, nearestCellPos);

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

	float minHeight = 0.49;
	float maxHeight = 0.52;

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
	float blendWeight1 = float((uint(ex_Colour.y) >> 16)) / 65536.0;
	float blendWeight2 = float((uint(ex_Colour.y) & 0xFFFF)) / 65536.0;
	float blendWeight0 = clamp(1.0 - blendWeight1 - blendWeight2, 0.0, 1.0);

	{
		int m0 = matID0 % 4;
		int m1 = matID1 % 4;
		int m2 = matID2 % 4;

		float alpha = clamp((height - minHeight) / (maxHeight - minHeight), 0.0, 1.0);
		vec3 c0 = mix(lowCols[m0], highCols[m0], alpha);
		vec3 c1 = mix(lowCols[m1], highCols[m1], alpha);
		vec3 c2 = mix(lowCols[m2], highCols[m2], alpha);
		groundCol = c0;// * blendWeight0 + c1 * blendWeight1 + c2 * blendWeight2;
		groundCol = pow(groundCol, vec3(2.2));

		// groundCol = vec3(float(m0) / 4.0, float(matID1) / 4.0, float(matID2) / 4.0);
	}

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

		dirLightShadowOpacity = DoShadowMapping(uboConstant.dirLight, uboConstant.shadowSamplingData, ex_PositionWS, cascadeIndex, shadowMaps, NoL);
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
	fragmentColour.rgb *= 1.0-(pow(blendWeight1*2.0, 200.0));

    DrawDebugOverlay(albedo, N, roughness, metallic, diffuse, specular, ex_TexCoord,
     linDepth, dirLightShadowOpacity, cascadeIndex, ssao, /* inout */ fragmentColour);
}
