#version 450

layout (location = 0) in vec2 in_Position2D;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 2) in vec4 in_Color;
layout (location = 3) in vec4 in_ExtraVec4; // RG: char size in pixels, BA: char size normalized in screen-space
layout (location = 4) in int in_ExtraInt;   // Texture channel - NOTE: This attrib prevents all attrib interpolation from happening because it is integral (I think)

layout (location = 0) out VSO
{
    vec2 position;
    vec4 color;
    vec2 texCoord;
    vec2 charSizePixels;
    vec2 charSizeNorm;
	flat int channel;
} outputs;

void main()
{
	outputs.position = in_Position2D;
	outputs.color = in_Color;
	outputs.texCoord = in_TexCoord;
	outputs.charSizePixels = in_ExtraVec4.rg;
	outputs.charSizeNorm = in_ExtraVec4.ba;
	outputs.channel = in_ExtraInt;
}