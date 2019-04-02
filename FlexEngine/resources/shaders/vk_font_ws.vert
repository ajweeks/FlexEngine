#version 450

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec4 in_Color;
layout (location = 2) in vec3 in_Tangent;
layout (location = 3) in vec2 in_TexCoord;
layout (location = 4) in vec4 in_ExtraVec4; // RG: char size in pixels, BA: char size normalized in screen-space
layout (location = 5) in int in_ExtraInt;   // Texture channel

layout (location = 0) out VSO
{
    vec3 position;
    vec4 color;
    vec3 tangent;
    vec2 texCoord;
    vec2 charSizePixels;
    vec2 charSizeNorm;
	flat int channel;
} outputs;

void main()
{
	outputs.position = in_Position;
	outputs.color = in_Color;
	outputs.tangent = in_Tangent;
	outputs.texCoord = in_TexCoord;
	outputs.charSizePixels = in_ExtraVec4.rg;
	outputs.charSizeNorm = in_ExtraVec4.ba;
	outputs.channel = in_ExtraInt;
}