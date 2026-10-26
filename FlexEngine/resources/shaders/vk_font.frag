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

	float texValue = texture(in_Texture, inputs.texCoord)[inputs.channel];
	float shadowTexValue = texture(in_Texture, inputs.texCoord + shadow)[inputs.channel];

	// Change in the distance field per screen pixel, so edges are anti-aliased over one pixel at any scale
	// (a fixed smoothing width is blurry when magnified & aliased when minified)
	float pixelDist = max(length(vec2(dFdx(texValue), dFdy(texValue))), 1e-4);
	float a = clamp((texValue - threshold) / pixelDist + 0.5, 0.0, 1.0);
	// Drop shadows are softened further by the font's soften value
	float shadowA = smoothstep(threshold - soften - pixelDist * 0.5, threshold + soften + pixelDist * 0.5, shadowTexValue);

	// A dark shadow under dark text only thickens it (& fills the gaps between letters)
	float luminance = dot(inputs.colour.rgb, vec3(0.2126, 0.7152, 0.0722));
	shadowOpacity *= smoothstep(0.2, 0.5, luminance);

	float textAlpha = a * inputs.colour.a;
	float shadowAlpha = shadowA * shadowOpacity * inputs.colour.a;
	// Text over its (black) shadow
	float alpha = textAlpha + shadowAlpha * (1.0 - textAlpha);
	if (alpha <= 0.0)
	{
		discard;
	}
	out_Colour = vec4(inputs.colour.rgb * (textAlpha / alpha), alpha);
}
