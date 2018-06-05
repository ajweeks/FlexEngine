#version 330

uniform mat4 in_ProjMatrix;

in vec2 in_Position2D;
in vec2 in_TexCoord;
in vec4 in_Color_32;

out vec2 ex_TexCoord;
out vec4 ex_Color;

void main()
{
	ex_TexCoord = in_TexCoord;
	ex_Color = in_Color_32;
	gl_Position = in_ProjMatrix * vec4(in_Position2D.xy, 0, 1);
}