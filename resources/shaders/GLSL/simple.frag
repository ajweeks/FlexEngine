#version 450

uniform vec3 lightDir = vec3(0.5, 1.0, 1.0);

in vec4 ex_Color;
in vec3 ex_Normal;
in vec2 ex_TexCoord;

out vec4 fragmentColor;

void main() 
{
	float lightIntensity = max(dot(ex_Normal, -normalize(lightDir)), 0.0);
	lightIntensity = lightIntensity * 0.75 + 0.25;

	// Flat shading:
	//fragmentColor = ex_Color;

	//fragmentColor = lightIntensity * ex_Color;
	fragmentColor = lightIntensity * vec4(ex_TexCoord.x, ex_TexCoord.y, 0, 0);
}
