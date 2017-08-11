#version 400

uniform mat4 in_MVP;

in vec3 in_Position;
in vec4 in_Color;

out vec4 ex_Color;

void main() 
{
	gl_Position = in_MVP * vec4(in_Position, 1.0);
	
	ex_Color = in_Color;
}
