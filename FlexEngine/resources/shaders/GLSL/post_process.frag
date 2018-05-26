#version 450

uniform sampler2D in_Texture;

in vec2 ex_TexCoord;
in vec4 ex_Color;

out vec4 out_Color;

void main()
{
	ivec2 texSize = textureSize(in_Texture, 0);
	vec2 pixelSize = vec2(1.0 / texSize.x, 1.0 / texSize.y);


	// Edge detection
	{
		//float dx =  (texture(in_Texture, ex_TexCoord).r - texture(in_Texture, ex_TexCoord + vec2(pixelSize.x, 0)).r) +
		//			(texture(in_Texture, ex_TexCoord).r - texture(in_Texture, ex_TexCoord + vec2(-pixelSize.x, 0)).r);
		//float dy =	(texture(in_Texture, ex_TexCoord).r - texture(in_Texture, ex_TexCoord + vec2(0.0, pixelSize.y)).r) + 
		//			(texture(in_Texture, ex_TexCoord).r - texture(in_Texture, ex_TexCoord + vec2(0.0, -pixelSize.y)).r);
		//
		//float edges = max(abs(dx), abs(dy));
		//color -= edges;
	}



	// Chromatic abberation
	{
		//float abberationScale = 0.15;
		//float distFromCenterN = length(ex_TexCoord - vec2(0.5, 0.5)) * 2.0;
		//distFromCenterN = distFromCenterN * distFromCenterN;
		//out_Color = vec4(
		//	texture(in_Texture, ex_TexCoord - (distFromCenterN * vec2(-0.0025, 0.005) * abberationScale)).r,
		//	texture(in_Texture, ex_TexCoord - (distFromCenterN * vec2(0.0025, 0.005) * abberationScale)).g,
		//	texture(in_Texture, ex_TexCoord - (distFromCenterN * vec2(0.0, -0.005) * abberationScale)).b,
		//	1);
		//return;
	}



	vec4 color = ex_Color * texture(in_Texture, ex_TexCoord);

	color.rgb = color.rgb / (color.rgb + vec3(1.0f)); // Reinhard tone-mapping
	color.rgb = pow(color.rgb, vec3(1.0f / 2.2f)); // Gamma correction

	out_Color = color;
}
