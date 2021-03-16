#version 450

#define NUM_POINT_LIGHTS 8

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (constant_id = 2) const int QUALITY_LEVEL = 1;
layout (constant_id = 3) const int NUM_CASCADES = 4;

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

layout (binding = 0) uniform UBOConstant
{
	vec4 camPos;
	mat4 invView;
	mat4 viewProjection;
	mat4 invProj;
	DirectionalLight dirLight;
	PointLight pointLights[NUM_POINT_LIGHTS];
	ShadowSamplingData shadowSamplingData;
	SSAOSamplingData ssaoData;
	float zNear;
	float zFar;
} uboConstant;

layout (location = 0) in vec2 ex_TexCoord;
layout (location = 1) in vec4 ex_Colour;
layout (location = 2) in vec3 ex_NormalWS;
layout (location = 3) in vec3 ex_PositionWS;

layout (binding = 2) uniform sampler2D albedoSampler;
layout (binding = 3) uniform sampler2D ssaoBuffer;
layout (binding = 4) uniform sampler2DArray shadowMaps;

layout (location = 0) out vec4 fragmentColour;

const mat4 biasMat = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 
);

vec3 ReconstructVSPosFromDepth(vec2 uv, float depth)
{
	float x = uv.x * 2.0f - 1.0f;
	float y = (1.0f - uv.y) * 2.0f - 1.0f;
	vec4 pos = vec4(x, y, depth, 1.0f);
	vec4 posVS = uboConstant.invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return posNDC;
}

vec3 ReconstructWSPosFromDepth(vec2 uv, float depth)
{
	float x = uv.x * 2.0f - 1.0f;
	float y = (1.0f - uv.y) * 2.0f - 1.0f;
	vec4 pos = vec4(x, y, depth, 1.0f);
	vec4 posVS = uboConstant.invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return (uboConstant.invView * vec4(posNDC, 1)).xyz;
}

void main() 
{
	vec3 albedo = ex_Colour.rgb * texture(albedoSampler, ex_TexCoord).rgb;
	vec3 N = normalize(ex_NormalWS);

	float dist = clamp(length(uboConstant.camPos.xyz - ex_PositionWS)*0.0002,0.15,1.0);

	dist = smoothstep(dist, 0.0, 0.1);

	vec3 V = normalize(uboConstant.camPos.xyz - ex_PositionWS);

	vec3 L = normalize(vec3(0.57, 0.57, 0.57));
	float light = dot(N, L) * 0.5 + 0.5; // Wrapped NoL
	float fresnel = pow(1.0 - dot(N, V), 6.0);

    float invDist = 1.0f/(uboConstant.zFar-uboConstant.zNear);
	vec3 viewPos = ReconstructVSPosFromDepth(gl_FragCoord.xy, ex_PositionWS.z);
	float linDepth = (viewPos.z-uboConstant.zNear)*invDist;

    float ssao = (uboConstant.ssaoData.enabled == 1 ? texture(ssaoBuffer, gl_FragCoord.xy).r : 1.0f);

	// fragColour = vec4(vec3(pow(ssao, uboConstant.ssaoPowExp)), 1); return;

	uint cascadeIndex = 0;
	for (uint i = 0; i < NUM_CASCADES; ++i)
	{
		if (linDepth > uboConstant.shadowSamplingData.cascadeDepthSplits[i])
		{
			cascadeIndex = i + 1;
		}
	}

	if (uboConstant.dirLight.enabled != 0)
	{
		vec3 L = normalize(uboConstant.dirLight.direction);
		vec3 radiance = uboConstant.dirLight.colour.rgb * uboConstant.dirLight.brightness;
		float NoL = max(dot(N, L), 0.0);

		float dirLightShadowOpacity = 1.0;
		if (uboConstant.dirLight.castShadows != 0)
		{
			vec4 transformedShadowPos = (biasMat * uboConstant.shadowSamplingData.cascadeViewProjMats[cascadeIndex]) * vec4(ex_PositionWS, 1.0);
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
					float shadowSampleContrib = uboConstant.dirLight.shadowDarkness / ((sampleRadius*2 + 1) * (sampleRadius*2 + 1));
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
						dirLightShadowOpacity = 1.0 - uboConstant.dirLight.shadowDarkness;
					}
				}
			}
		}

		light *= dirLightShadowOpacity;
	}

	vec3 groundCol;

	float min = 0.45;
	float max = 0.52;
	vec3 lowCol = vec3(0.13, 0.11, 0.041);
	vec3 highCol = vec3(0.08, 0.16, 0.04);// vec3(0.00, 0.04, 0.01);
	if (ex_Colour.r > max)
	{
		groundCol = highCol;
	}
	else if (ex_Colour.r < min)
	{
		groundCol = lowCol;
	}
	else
	{
		float alpha = (ex_Colour.r - min) / (max - min);
		groundCol = mix(lowCol, highCol, clamp(alpha, 0.0, 1.0));
	}
	groundCol *= light * ssao;
	groundCol += (fresnel * 1.2) * groundCol;
	fragmentColour = vec4(mix(groundCol, vec3(0), pow(dist, 0.2)), 1.0);
	// fragmentColour = vec4(ex_Colour.rgb, 1.0);

#if 0
	switch (cascadeIndex)
	{
		case 0: fragmentColour *= vec4(0.85f, 0.4f, 0.3f, 0.0f); return;
		case 1: fragmentColour *= vec4(0.2f, 1.0f, 1.0f, 0.0f); return;
		case 2: fragmentColour *= vec4(1.0f, 1.0f, 0.2f, 0.0f); return;
		case 3: fragmentColour *= vec4(0.4f, 0.8f, 0.2f, 0.0f); return;
		default: fragmentColour = vec4(1.0f, 0.0f, 1.0f, 0.0f); return;
	}
#endif
}
