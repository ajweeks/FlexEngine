#version 450

layout (location = 0) in vec2 in_Position2D;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 2) in vec4 in_Color;
layout (location = 3) in vec4 in_ExtraVec4; // Char size - vec4 because it uses "extra vec4s" slot
layout (location = 4) in int in_ExtraInt;   // Texture channel

out VSO
{
    vec2 position;
    vec4 color;
    vec2 texCoord;
    vec2 charSize;
	flat int channel;
} outputs;

void main()
{
	outputs.position = in_Position2D;
	outputs.color = in_Color;
	outputs.texCoord = in_TexCoord;
	outputs.charSize = in_ExtraVec4.xy;
	outputs.channel = in_ExtraInt;
}