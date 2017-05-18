#version 450

//uniform float in_Time;

in vec3 ex_Color;

out vec4 fragmentColor;

void main() {
	fragmentColor = vec4(ex_Color, 1.0);
	//fragmentColor.r = fract(fract(fragmentColor.r * 1.2 + in_Time * 0.6) + fract((fragmentColor.b) * 3.5));
	//fragmentColor.rgb *= vec3((gl_FragCoord.x / 1000.0) + 0.3);
}
