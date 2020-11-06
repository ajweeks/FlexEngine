#version 450

layout (location = 0) out vec4 out_Colour;

layout (location = 0) in GSO
{
	vec4 colour;
	vec2 texCoord;
} inputs;

layout (binding = 2) uniform sampler2D in_Texture;

void main()
{
	// out_Colour = vec4(inputs.colour); return;
	// out_Colour = vec4(inputs.texCoord, 0, 1); return;

	if (length(inputs.texCoord*2.0-1.0) > 0.5)
	{
		discard;
	}

	vec4 texColour = texture(in_Texture, inputs.texCoord);

	out_Colour = texColour * inputs.colour;
	//out_Colour = vec4(inputs.texCoord, 0, 1);
	//out_Colour = inputs.colour;
}