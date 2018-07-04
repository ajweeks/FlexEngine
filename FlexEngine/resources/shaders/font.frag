#version 450

uniform sampler2D in_Texture;

uniform float threshold = 0.5;
//uniform float outline = 0.2;
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
	vec4 color;

	float texValue = texture(in_Texture, inputs.texCoord)[inputs.channel];
	if (texValue < threshold - soften)
	{
		discard;
	}

	float max = threshold + soften;
	float min = threshold - soften;
	float a = (texValue - min) / (max - min);

	color = inputs.color;
	color.a = mix(0, inputs.color.a, a);
	out_Color = color;
}