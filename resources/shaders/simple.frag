#version 400

//uniform sampler2D texTest;
uniform float in_Time;

in vec4 ex_Color;

out vec4 fragmentColor;

void main() {
	fragmentColor = ex_Color;
	fragmentColor.r += in_Time;
	fragmentColor.r = fract(fragmentColor.r * 5) + fract((fragmentColor.g) * 3);
}

