#version 450

in vec3 ex_Color;

out vec4 fragmentColor;

void main() {
	fragmentColor = vec4(ex_Color, 1.0);
}
