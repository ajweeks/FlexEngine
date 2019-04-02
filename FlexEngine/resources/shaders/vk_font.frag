#version 450

layout (location = 0) out vec4 out_Color;

layout (location = 0) in GSO
{
	flat int channel;
	vec4 color;
	vec2 texCoord;
} inputs;

layout (binding = 0) uniform UBODynamic
{
	mat4 model;

	vec4 thresholdXshadowYZsoftenW;
	// threshold = 0.5;
	// shadow = vec2(-0.01, -0.008);
	// soften = 0.035;

	vec2 texSize;
} uboDynamic;

layout (binding = 1) uniform sampler2D in_Texture;

void main()
{
	
	float threshold = uboDynamic.thresholdXshadowYZsoftenW.x;
	vec2 shadow = uboDynamic.thresholdXshadowYZsoftenW.yz;
	float soften = uboDynamic.thresholdXshadowYZsoftenW.w;

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