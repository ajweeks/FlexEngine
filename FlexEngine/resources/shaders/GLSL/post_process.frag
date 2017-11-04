#version 450

uniform sampler2D in_Texture;

in vec2 ex_TexCoord;
in vec3 ex_Color;

out vec4 out_Color;

void main()
{
	vec3 color = ex_Color * texture(in_Texture, ex_TexCoord).rgb;

	color = color / (color + vec3(1.0f)); // Reinhard tone-mapping
	color = pow(color, vec3(1.0f / 2.2f)); // Gamma correction

	out_Color = vec4(color, 1);
}
