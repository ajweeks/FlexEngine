#version 450

uniform sampler2D in_Texture;
uniform float lumaThresholdMax = 1.0/3.0;
uniform float lumaThresholdMin = 1.0/16.0;
uniform float mulReduce = 1.0/8.0;
uniform float minReduce = 1.0/128.0;
uniform float maxSpan = 8.0;
uniform vec2 texelStep;

uniform bool bDEBUGShowEdges = false;

in vec2 ex_TexCoord;

out vec4 out_Color;

// https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf
void main()
{
	vec3 color = texture(in_Texture, ex_TexCoord).rgb;

	vec3 rgbNW = textureOffset(in_Texture, ex_TexCoord, ivec2(-1, 1)).rgb;
	vec3 rgbNE = textureOffset(in_Texture, ex_TexCoord, ivec2(1, 1)).rgb;
	vec3 rgbSW = textureOffset(in_Texture, ex_TexCoord, ivec2(-1, -1)).rgb;
	vec3 rgbSE = textureOffset(in_Texture, ex_TexCoord, ivec2(1, -1)).rgb;
	
	// TODO: Profile difference when using just R & G: float luma = (rgb.y * (0.587/0.299) + rgb.x);
	const vec3 toLuma = vec3(0.299, 0.587, 0.114);

	float lumaNW = dot(rgbNW, toLuma);
	float lumaNE = dot(rgbNE, toLuma);
	float lumaSW = dot(rgbSW, toLuma);
	float lumaSE = dot(rgbSE, toLuma);
	float lumaM = dot(color, toLuma);

	float lumaMin = min(lumaM, min(min(lumaNE, lumaNW), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNE, lumaNW), max(lumaSW, lumaSE)));

	// Only apply AA to high contrast regions
	if ((lumaMax - lumaMin) < max(lumaThresholdMin, lumaMax * lumaThresholdMax))
	{
		out_Color = vec4(color, 1);
		return;
	}

	vec2 samplingDirection = vec2(
		-((lumaNW + lumaNE) - (lumaSW + lumaSE)),
		((lumaNW + lumaSW) - (lumaNE + lumaSE)));

	float samplingDirectionReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 + mulReduce, minReduce);

	float minSamplingDirectionFactor = 1.0 / (min(abs(samplingDirection.x), abs(samplingDirection.y)) + samplingDirectionReduce);

	samplingDirection = clamp(samplingDirection * minSamplingDirectionFactor, vec2(-maxSpan, -maxSpan), vec2(maxSpan, maxSpan)) * texelStep;

	vec3 rgbSampleNeg = texture(in_Texture, ex_TexCoord + samplingDirection * (1.0/3.0 - 0.5)).rgb;
	vec3 rgbSamplePos = texture(in_Texture, ex_TexCoord + samplingDirection * (2.0/3.0 - 0.5)).rgb;

	vec3 rgbTwoTap = (rgbSamplePos + rgbSampleNeg) * 0.5;

	vec3 rgbSampleNegOuter = texture(in_Texture, ex_TexCoord + samplingDirection * (0.0/3.0 - 0.5)).rgb;
	vec3 rgbSamplePosOuter = texture(in_Texture, ex_TexCoord + samplingDirection * (3.0/3.0 - 0.5)).rgb;

	vec3 rgbFourTap = (rgbSamplePosOuter + rgbSampleNegOuter) * 0.25 + rgbTwoTap * 0.25;

	float lumaFourTap = dot(rgbFourTap, toLuma);

	if (lumaFourTap < lumaMin || lumaFourTap > lumaMax)
	{
		out_Color = vec4(rgbTwoTap, 1.0);
	}
	else
	{
		out_Color = vec4(rgbFourTap, 1.0);
	}

	if (bDEBUGShowEdges)
	{
		out_Color.r = 1.0;
	}
}
