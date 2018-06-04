#version 450

in vec2 in_Position2D;
// TODO: Use index to determine tex coord
in vec2 in_TexCoord;

out vec2 ex_TexCoord;

void main()
{
	ex_TexCoord = in_TexCoord;
	gl_Position = vec4(in_Position2D, 0, 1);
}