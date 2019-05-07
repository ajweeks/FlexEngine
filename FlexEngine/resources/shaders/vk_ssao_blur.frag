#version 450

layout (location = 0) out float out_Color;

layout (location = 0) in vec2 ex_UV;

layout (binding = 0) uniform UBOConstant
{
	// SSAO Blur Data
	int ssaoBlurRadius;
	int pad;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	vec2 ssaoTexelOffset;
} uboDyanmic;

layout (binding = 2) uniform sampler2D in_SSAO;

void main()
{
	int sampleCount = 0;
	float sum = 0.0f;
	for (int i = -uboConstant.ssaoBlurRadius; i <= uboConstant.ssaoBlurRadius; i++) 
	{
		vec2 offset = uboDyanmic.ssaoTexelOffset * float(i);
		sum += texture(in_SSAO, ex_UV + offset).r;
		++sampleCount;
	}
	out_Color = clamp(sum / float(sampleCount), 0.0f, 1.0f);
}
