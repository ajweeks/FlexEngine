#version 450

layout (location = 0) out float out_Color;

layout (location = 0) in vec2 ex_UV;

// SSAO Blur Data Constant
uniform int ssaoBlurRadius = 4;

// SSAO Blur Data Dynamic
uniform vec2 ssaoTexelOffset;

layout (binding = 0) uniform sampler2D ssaoFrameBufferSampler;
layout (binding = 1) uniform sampler2D normalRoughnessFrameBufferSampler;
layout (binding = 2) uniform sampler2D in_Depth;

void main()
{
	float ourDepth = texture(in_Depth, ex_UV).r;
	vec3 ourNormal = normalize(texture(normalRoughnessFrameBufferSampler, ex_UV).rgb * 2.0f - 1.0f);

	int sampleCount = 0;
	float sum = 0.0f;
	for (int i = -ssaoBlurRadius; i <= ssaoBlurRadius; i++) 
	{
		vec2 offset = ssaoTexelOffset * float(i);
		float depth = texture(in_Depth, ex_UV + offset).r;
		vec3 normal = normalize(texture(normalRoughnessFrameBufferSampler, ex_UV + offset).rgb * 2.0f - 1.0f);
		if (abs(ourDepth - depth) < 0.00002f && dot(ourNormal, normal) > 0.5f)
		{
			sum += texture(ssaoFrameBufferSampler, ex_UV + offset).r;
			++sampleCount;
		}
	}
	out_Color = clamp(sum / float(sampleCount), 0.0f, 1.0f);
}
