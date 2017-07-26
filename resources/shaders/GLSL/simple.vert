#version 450

uniform mat4 in_MVP;
uniform mat4 in_ModelInverse;

in vec3 in_Position;
in vec4 in_Color;
in vec3 in_Normal;
in vec2 in_TexCoord;

out vec4 ex_Color;
out vec3 ex_Normal;
out vec2 ex_TexCoor;

void main() 
{
	gl_Position = in_MVP * vec4(in_Position, 1.0);
	ex_Normal = mat3(transpose(in_ModelInverse)) * in_Normal;
	ex_Color = in_Color;
	ex_TexCoor = in_TexCoord;
}
