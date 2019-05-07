#version 450

layout (location = 0) out float out_Color;

layout (location = 0) in vec2 ex_UV;

layout (binding = 0) uniform sampler2D ssaoFrameBufferSampler;

// SSAO Blur Data
uniform int ssaoBlurRadius = 4;

void main()
{
	const float sampleCount = (ssaoBlurRadius*2+1) * (ssaoBlurRadius*2+1);
	vec2 texelSize = 1.0 / vec2(textureSize(ssaoFrameBufferSampler, 0));
	float sum = 0.0;
	for (int x = -ssaoBlurRadius; x <= ssaoBlurRadius; x++) 
	{
		for (int y = -ssaoBlurRadius; y <= ssaoBlurRadius; y++) 
		{
			vec2 offset = vec2(float(x), float(y)) * texelSize;
			sum += texture(ssaoFrameBufferSampler, ex_UV + offset).r;
		}
	}
	out_Color = clamp(sum / sampleCount, 0.0f, 1.0f);
}
