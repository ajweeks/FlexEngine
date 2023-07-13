#version 450

layout (binding = 0) uniform UBODynamic
{
	vec4 colourMultiplier;
	mat4 contrastBrightnessSaturation;
} uboDynamic;

layout (binding = 1) uniform sampler2D in_Texture;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Colour;

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
		//colour -= edges;
	}



	// Chromatic aberration
	{
		//float abberationScale = 0.15;
		//float distFromCenterN = length(ex_TexCoord - vec2(0.5, 0.5)) * 2.0;
		//distFromCenterN = distFromCenterN * distFromCenterN;
		//out_Colour = vec4(
		//	texture(in_Texture, ex_TexCoord - (distFromCenterN * vec2(-0.0025, 0.005) * abberationScale)).r,
		//	texture(in_Texture, ex_TexCoord - (distFromCenterN * vec2(0.0025, 0.005) * abberationScale)).g,
		//	texture(in_Texture, ex_TexCoord - (distFromCenterN * vec2(0.0, -0.005) * abberationScale)).b,
		//	1);
		//return;
	}



	vec4 colour = texture(in_Texture, ex_TexCoord);

	colour.rgb = (uboDynamic.contrastBrightnessSaturation * vec4(colour.rgb, 1)).rgb;

	colour.rgb = colour.rgb / (colour.rgb + vec3(1.0f)); // Reinhard tone-mapping

	out_Colour = uboDynamic.colourMultiplier * colour;
}
