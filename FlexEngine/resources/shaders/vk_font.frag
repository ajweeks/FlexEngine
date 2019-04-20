#version 450

layout (location = 0) out vec4 out_Color;

layout (location = 0) in GSO
{
	vec4 color;
	vec2 texCoord;
	flat int channel;
} inputs;

layout (binding = 0) uniform UBODynamic
{
	mat4 model;
	vec4 thresholdXshadowYZsoftenW;
	vec2 texSize;
} uboDynamic;

layout (binding = 1) uniform sampler2D in_Texture;

void main()
{
	float threshold = uboDynamic.thresholdXshadowYZsoftenW.x;
	vec2 shadow = uboDynamic.thresholdXshadowYZsoftenW.yz;
	float soften = uboDynamic.thresholdXshadowYZsoftenW.w;

	// out_Color = vec4(inputs.color); return;
	// out_Color = vec4(inputs.texCoord, 0, 1); return;

	float shadowTexValue = texture(in_Texture, inputs.texCoord + shadow)[inputs.channel];
	float texValue = texture(in_Texture, inputs.texCoord)[inputs.channel];
	if (texValue < threshold - soften && shadowTexValue < threshold - soften)
	{
		discard;
	}

	float max = threshold + soften;
	float min = threshold - soften;
	float a = clamp((texValue - min) / (max - min), 0, 1);
	float shadowA = clamp((shadowTexValue - min) / (max - min), 0, 1);

	vec4 color = inputs.color;
	color.a = mix(0.0, inputs.color.a, a);
	vec4 shadowColor = vec4(0.0);
	shadowColor.a = mix(0.0, 0.5, shadowA * inputs.color.a);
	out_Color = mix(shadowColor, color, color.a);
}