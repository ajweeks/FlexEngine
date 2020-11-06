#version 450

layout (location = 0) out vec4 out_Colour;

layout (location = 0) in GSO
{
	vec4 colour;
	vec2 texCoord;
	flat int channel;
} inputs;

layout (binding = 0) uniform UBODynamic
{
	mat4 model;
	vec4 thresholdXshadowYZsoftenShadowOpacityW;
	vec2 texSize;
} uboDynamic;

layout (binding = 1) uniform sampler2D in_Texture;

void UnpackU32To2Float(uint u1, out float outF1, out float outF2)
{
	outF1 = (u1 >> 16) / 65535.0f;
	outF2 = (u1 & 0xFFFFu) / 65535.0f;
}

void main()
{
	float threshold = uboDynamic.thresholdXshadowYZsoftenShadowOpacityW.x;
	vec2 shadow = uboDynamic.thresholdXshadowYZsoftenShadowOpacityW.yz;
	uint softenOpacity = uint(uboDynamic.thresholdXshadowYZsoftenShadowOpacityW.w);
	float soften, shadowOpacity;
	UnpackU32To2Float(softenOpacity, soften, shadowOpacity);

	// out_Colour = vec4(inputs.colour); return;
	// out_Colour = vec4(inputs.texCoord, 0, 1); return;

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

	vec4 colour = inputs.colour;
	colour.a = mix(0.0, inputs.colour.a, a);
	vec4 shadowColour = vec4(0.0);
	shadowColour.a = mix(0.0, shadowOpacity, shadowA * inputs.colour.a);
	out_Colour = mix(shadowColour, colour, colour.a);
}