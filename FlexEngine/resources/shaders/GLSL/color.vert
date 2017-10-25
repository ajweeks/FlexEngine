#version 400

uniform mat4 view;
uniform mat4 projection;

uniform mat4 model;

in vec3 in_Position;
in vec4 in_Color;

out vec4 ex_Color;

void main() 
{
	gl_Position = projection * view * model * vec4(in_Position, 1.0);
	
	ex_Color = in_Color;
}
