#version 450

layout (location = 0) out float out_Color;

layout (location = 0) in vec2 ex_UV;

layout (binding = 0) uniform UBOConstant
{
	// SSAO Blur Data
	int ssaoBlurRadius;
} uboConstant;

layout (binding = 1) uniform sampler2D in_SSAO;

void main()
{
	int range = uboConstant.ssaoBlurRadius;
	const float sampleCount = (range*2+1) * (range*2+1);
	vec2 texelSize = 2.0 / vec2(textureSize(in_SSAO, 0));
	float sum = 0.0;
	for (int x = -range; x <= range; x++) 
	{
		for (int y = -range; y <= range; y++) 
		{
			vec2 offset = vec2(float(x), float(y)) * texelSize;
			sum += texture(in_SSAO, ex_UV + offset).r;
		}
	}
	out_Color = clamp(sum / sampleCount, 0.0f, 1.0f);
}
