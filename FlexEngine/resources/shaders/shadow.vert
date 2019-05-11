#version 400

uniform mat4 lightViewProj;

uniform mat4 model;

layout (location = 0) in vec3 in_Position;

void main() 
{
	gl_Position = lightViewProj * model * vec4(in_Position, 1.0);
}
