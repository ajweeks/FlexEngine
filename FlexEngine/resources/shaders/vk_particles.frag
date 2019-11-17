#version 450

layout (location = 0) out vec4 out_Color;

layout (location = 0) in GSO
{
	vec4 color;
	vec2 texCoord;
} inputs;

layout (binding = 1) uniform sampler2D in_Texture;

void main()
{
	// out_Color = vec4(inputs.color); return;
	// out_Color = vec4(inputs.texCoord, 0, 1); return;

	vec4 texColor = texture(in_Texture, inputs.texCoord);

	out_Color = texColor * inputs.color;
}