#version 450

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 2) in vec4 in_Color;
layout (location = 3) in vec3 in_Velocity; // Unused but necessary for compute pass to store inter-frame
layout (location = 4) in vec4 in_ExtraVec4; // X: size

layout (location = 0) out VSO
{
    vec3 position;
    vec2 texCoord;
    vec4 color;
    float size;
} outputs;

void main()
{
	outputs.position = in_Position;
	outputs.texCoord = in_TexCoord;
	outputs.color = in_Color;
	outputs.size = in_ExtraVec4.x;
}