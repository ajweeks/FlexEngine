#version 430

in vec4 in_Color;
in vec4 in_Position;
out vec4 ex_Color;

uniform mat4 MVP;

void main() {
	gl_Position = MVP * in_Position;
	ex_Color = in_Color;
}
