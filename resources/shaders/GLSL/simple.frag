#version 450

uniform vec3 lightDir = vec3(1.0, 1.0, 1.0);

in vec4 ex_Color;
in vec3 ex_Normal;
in vec2 ex_TexCoor;

out vec4 fragmentColor;

void main() 
{
	float lightIntensity = max(dot(ex_Normal, -normalize(lightDir)), 0.0f);
	fragmentColor = lightIntensity * ex_Color;
}
