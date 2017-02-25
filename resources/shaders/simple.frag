#version 400

uniform sampler2D texTest;
uniform float time;

in vec2 ex_TexCoord;

out vec4 fragmentColor;

void main() {
	if (ex_TexCoord.y < 0.5)
	{
		fragmentColor = texture(texTest, ex_TexCoord);
	}
	else
	{
		vec2 newTexCoord = ex_TexCoord;
		float dPos = sin(ex_TexCoord.y * 100) * 0.035;
		newTexCoord.x += dPos;
		newTexCoord.y = 1.0 - newTexCoord.y;
		fragmentColor = texture(texTest, newTexCoord);
		fragmentColor.rgb *= 0.8f; // Darken
	}
}
