#version 400

in vec4 in_Position;
in vec2 in_TexCoord;

out vec2 ex_TexCoord;

uniform mat4 MVP;

void main() {
	gl_Position = in_Position;//MVP * in_Position;
	ex_TexCoord = in_TexCoord;
}
