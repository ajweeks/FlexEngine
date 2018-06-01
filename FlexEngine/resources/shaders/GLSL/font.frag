#version 450

uniform sampler2D in_Texture;

uniform float threshold = 0.5;
uniform float outline = 0.06;

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

	float texValue = texture(in_Texture, vec2(inputs.texCoord.x, 1-inputs.texCoord.y))[inputs.channel];
	if (texValue < threshold)
	{
		discard;
	}

	if (texValue < threshold + outline)
	{
		color = vec4(0, 0, 0, 1);
	}
	else
	{
		color = inputs.color;
	}

	out_Color = color;
}