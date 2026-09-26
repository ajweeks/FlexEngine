#version 450

#include "vk_misc.glsl"

layout (location = 0) out float fragColour;

layout (location = 0) in vec2 ex_TexCoord;

layout (binding = 0) uniform UBOConstant
{
	mat4 projection;
	mat4 invProj;
	// SSAO Gen Data
	vec4 samples[SSAO_KERNEL_SIZE];
	float ssaoRadius;
} uboConstant;

layout (binding = 1) uniform sampler2D in_Depth;
layout (binding = 2) uniform sampler2D in_Normal;
layout (binding = 3) uniform sampler2D in_Noise;

vec3 reconstructVSPosFromDepth(vec2 uv)
{
	float depth = texture(in_Depth, uv).r;
	float x = uv.x * 2.0f - 1.0f;
	float y = (1.0f - uv.y) * 2.0f - 1.0f;
	vec4 pos = vec4(x, y, depth, 1.0f);
	vec4 posVS = uboConstant.invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return posNDC;
}

void main()
{
    float depth = texture(in_Depth, ex_TexCoord).r;
	
	if (depth == 0.0f)
	{
		fragColour = 1.0f;
		return;
	}

	// G-buffer stores signed view-space normals (float format), no decode needed
	vec3 normal = normalize(texture(in_Normal, ex_TexCoord).rgb);

	vec3 posVS = reconstructVSPosFromDepth(ex_TexCoord);

	ivec2 depthTexSize = textureSize(in_Depth, 0); 
	ivec2 noiseTexSize = textureSize(in_Noise, 0);
	float renderScale = 0.5; // SSAO is rendered at 0.5x scale
	vec2 noiseUV = vec2(float(depthTexSize.x)/float(noiseTexSize.x), float(depthTexSize.y)/float(noiseTexSize.y)) * ex_TexCoord * renderScale;
	// noiseUV += vec2(0.5);
	vec3 randomVec = texture(in_Noise, noiseUV).xyz;
	
	vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	vec3 bitangent = cross(tangent, normal);
	mat3 TBN = mat3(tangent, bitangent, normal);

	float bias = 0.025f;

	float occlusion = 0.0f;
	for (uint i = 0; i < SSAO_KERNEL_SIZE; i++)
	{
		vec3 samplePos = TBN * uboConstant.samples[i].xyz;
		samplePos = posVS + samplePos * uboConstant.ssaoRadius; 

		vec4 offset = vec4(samplePos, 1.0f);
		offset = uboConstant.projection * offset;
		offset.xy /= offset.w;
		offset.xy = offset.xy * 0.5f + 0.5f;
		offset.y = 1.0f - offset.y;
		
		vec3 reconstructedPos = reconstructVSPosFromDepth(offset.xy);

		// Occluded when the visible surface at the sample's screen position is in front of the sample
		// Range check fades out contributions from geometry far in front of this fragment (e.g. silhouette edges)
		float rangeCheck = smoothstep(0.0f, 1.0f, uboConstant.ssaoRadius / abs(posVS.z - reconstructedPos.z));
		occlusion += (reconstructedPos.z <= samplePos.z - bias ? 1.0f : 0.0f) * rangeCheck;
	}
	occlusion = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));
	
	fragColour = occlusion;
}
