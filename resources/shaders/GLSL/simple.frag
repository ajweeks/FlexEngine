#version 450

uniform vec3 lightDir = vec3(-0.577f, -0.577f, 0.577f);

in vec4 ex_Color;
in vec3 ex_Normal;
in vec2 ex_TexCoord;

out vec4 fragmentColor;

void main() 
{
	float lightIntensity = dot(ex_Normal, -lightDir) + ex_TexCoord.x / 100.0f;
	fragmentColor = ex_Color * lightIntensity;
}
