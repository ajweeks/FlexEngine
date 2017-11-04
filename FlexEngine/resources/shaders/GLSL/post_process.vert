#version 450

uniform float verticalScale = 1; // Set to -1 to flip vertically

in vec2 in_Position2D;
in vec2 in_TexCoord;
in vec3 in_Color;

out vec2 ex_TexCoord;
out vec3 ex_Color;

void main()
{
	ex_TexCoord = in_TexCoord;
	ex_Color = in_Color;
	gl_Position = vec4(in_Position2D, 0, 1);
	gl_Position.y *= verticalScale;
}