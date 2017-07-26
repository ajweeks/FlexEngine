#version 450

uniform mat4 in_MVP;

in vec3 in_Position;
in vec4 in_Color;
in vec3 in_Normal;
in vec2 in_TexCoord;

out vec4 ex_Color;
out vec3 ex_Normal;
out vec2 ex_TexCoord;

void main() 
{
	gl_Position = in_MVP * vec4(in_Position, 1.0);
	ex_Color = in_Color;
	ex_Normal =  in_Normal;
	ex_TexCoord = in_TexCoord;
}
