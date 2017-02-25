#version 400

uniform sampler2D texTest;

in vec3 ex_Color;
in vec2 ex_TexCoord;

out vec4 fragmentColor;

void main() {
	fragmentColor = texture(texTest, ex_TexCoord) * vec4(ex_Color, 1.0);
}
