#version 450

uniform sampler2D in_Texture;

uniform float threshold = 0.5;
uniform vec2 shadow = vec2(-0.01, -0.008);
uniform float soften = 0.035;

in GSO
{
	flat int channel;
	vec4 color;
	vec2 texCoord;
} inputs;

out vec4 out_Color;

void main()
{
	float shadowTexValue = texture(in_Texture, inputs.texCoord + shadow)[inputs.channel];
	float texValue = texture(in_Texture, inputs.texCoord)[inputs.channel];
	if (texValue < threshold - soften &&
		shadowTexValue < threshold - soften)
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