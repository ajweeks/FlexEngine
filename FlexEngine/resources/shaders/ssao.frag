#version 450

layout (location = 0) out float fragColor;

layout (location = 0) in vec2 ex_UV;

const uint MAX_SSAO_KERNEL_SIZE = 64;

// SSAO Gen Data
uniform vec4 ssaoSamples[MAX_SSAO_KERNEL_SIZE];
uniform uint ssaoKernelSize = 64;
uniform float ssaoRadius = 3.0;

uniform mat4 projection;
uniform mat4 invProj;

layout (binding = 0) uniform sampler2D normalRoughnessFrameBufferSampler;
layout (binding = 1) uniform sampler2D in_Depth;
layout (binding = 2) uniform sampler2D in_Noise;

vec3 reconstructVSPosFromDepth(vec2 uv)
{
	float depth = texture(in_Depth, uv).r;
	float x = uv.x * 2.0f - 1.0f;
	float y = uv.y * 2.0f - 1.0f;
	vec4 pos = vec4(x, y, depth, 1.0f);
	vec4 posVS = invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return posNDC;
}

void main()
{
    float depth = texture(in_Depth, ex_UV).r;

	if (depth == 0.0f)
	{
		fragColor = 1.0f;
		return;
	}

	vec3 normal = normalize(texture(normalRoughnessFrameBufferSampler, ex_UV).rgb * 2.0 - 1.0);

	vec3 posVS = reconstructVSPosFromDepth(ex_UV);

	ivec2 depthTexSize = textureSize(in_Depth, 0); 
	ivec2 noiseTexSize = textureSize(in_Noise, 0);
	float renderScale = 0.5; // SSAO is rendered at 0.5x scale
	vec2 noiseUV = vec2(float(depthTexSize.x)/float(noiseTexSize.x), float(depthTexSize.y)/float(noiseTexSize.y)) * ex_UV * renderScale;
	// noiseUV += vec2(0.5);
	vec3 randomVec = texture(in_Noise, noiseUV).xyz;
	
	vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	vec3 bitangent = cross(tangent, normal);
	mat3 TBN = mat3(tangent, bitangent, normal);

	float bias = 0.01f;

	int sampleCount = 0;
	float occlusion = 0.0f;
	for (uint i = 0; i < ssaoKernelSize; i++)
	{
		vec3 samplePos = TBN * ssaoSamples[i].xyz; 
		samplePos = posVS + samplePos * ssaoRadius; 

		vec4 offset = vec4(samplePos, 1.0f);
		offset = projection * offset;
		offset.xy /= offset.w;
		offset.xy = offset.xy * 0.5f + 0.5f;
		if (offset.x >= 0.0 && offset.x <= 1.0f && offset.y >= 0.0f && offset.y <= 1.0f)
		{
			vec3 sampledNormal = texture(normalRoughnessFrameBufferSampler, offset.xy).xyz;
			vec3 reconstructedPos = reconstructVSPosFromDepth(offset.xy);
			if (dot(sampledNormal, normal) > 0.7)
			{
				++sampleCount;
			}
			else if (abs(reconstructedPos.z - posVS.z) < 3.0f)
			{
				float rangeCheck = smoothstep(0.0f, 1.0f, ssaoRadius / abs(reconstructedPos.z - samplePos.z));
				occlusion += (reconstructedPos.z <= samplePos.z - bias ? 1.0f : 0.0f) * rangeCheck;
				++sampleCount;
			}
		}
	}
	occlusion = 1.0 - (occlusion / float(sampleCount));
	
	fragColor = occlusion;
}
