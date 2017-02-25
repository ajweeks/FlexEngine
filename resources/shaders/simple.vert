#version 400

in vec4 in_Position;
in vec3 in_Color;
in vec2 in_TexCoord;

out vec3 ex_Color;
out vec2 ex_TexCoord;

uniform mat4 MVP;

void main() {
	gl_Position = MVP * in_Position;
	ex_Color = in_Color;
	ex_TexCoord = in_TexCoord;
}
