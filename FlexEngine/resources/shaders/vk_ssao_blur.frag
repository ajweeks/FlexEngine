#version 450

layout (location = 0) out float out_Color;

layout (location = 0) in vec2 ex_TexCoord;

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
	float ourDepth = texture(in_Depth, ex_TexCoord).r;
	vec3 ourNormal = normalize(texture(in_Normal, ex_TexCoord).rgb * 2.0f - 1.0f);

	int sampleCount = 0;
	float sum = 0.0f;
	for (int i = -uboConstant.ssaoBlurRadius; i <= uboConstant.ssaoBlurRadius; i++) 
	{
		vec2 offset = uboDyanmic.ssaoTexelOffset * float(i);
		float depth = texture(in_Depth, ex_TexCoord + offset).r;
		vec3 normal = normalize(texture(in_Normal, ex_TexCoord + offset).rgb * 2.0f - 1.0f);
		if (abs(ourDepth - depth) < 0.00002f && dot(ourNormal, normal) > 0.85f)
		{
			sum += texture(in_SSAO, ex_TexCoord + offset).r;
			++sampleCount;
		}
	}
	out_Color = clamp(sum / float(sampleCount), 0.0f, 1.0f);
}
