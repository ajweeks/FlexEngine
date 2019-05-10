#version 450

layout (location = 0) out float out_Color;

layout (location = 0) in vec2 ex_UV;

layout (binding = 0) uniform UBOConstant
{
	int ssaoBlurRadius;
	int pad;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	vec2 ssaoTexelOffset;
} uboDyanmic;

layout (binding = 2) uniform sampler2D in_Depth;
layout (binding = 3) uniform sampler2D in_SSAO;
layout (binding = 4) uniform sampler2D in_Normal;

void main()
{
	float ourDepth = texture(in_Depth, ex_UV).r;
	vec3 ourNormal = normalize(texture(in_Normal, ex_UV).rgb * 2.0f - 1.0f);

	int sampleCount = 0;
	float sum = 0.0f;
	for (int i = -uboConstant.ssaoBlurRadius; i <= uboConstant.ssaoBlurRadius; i++) 
	{
		vec2 offset = uboDyanmic.ssaoTexelOffset * float(i);
		float depth = texture(in_Depth, ex_UV + offset).r;
		vec3 normal = normalize(texture(in_Normal, ex_UV + offset).rgb * 2.0f - 1.0f);
		if (abs(ourDepth - depth) < 0.00002f && dot(ourNormal, normal) > 0.5f)
		{
			sum += texture(in_SSAO, ex_UV + offset).r;
			++sampleCount;
		}
	}
	out_Color = clamp(sum / float(sampleCount), 0.0f, 1.0f);
}
