#version 450

layout (binding = 0) uniform UBODynamic
{
	mat4 model;
	vec4 colorMultiplier;
	mat4 contrastBrightnessSaturation;
} uboDynamic;

layout (binding = 1) uniform sampler2D in_Texture;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Color;

float LinearToSRGB(float val)
{
	if (val < 0.0031308f)
    {
    	val *= 12.92f;
    }
    else
    {
    	val = 1.055f * pow(val,1.0f/2.4f) - 0.055f;
    }
    return val;
}

vec3 LinearToSRGB(vec3 val)
{
    return vec3(LinearToSRGB(val.x), LinearToSRGB(val.y), LinearToSRGB(val.z));
}

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



	// Chromatic aberration
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



	vec4 color = texture(in_Texture, ex_TexCoord);

	color.rgb = (uboDynamic.contrastBrightnessSaturation * vec4(color.rgb, 1)).rgb;

	color.rgb = color.rgb / (color.rgb + vec3(1.0f)); // Reinhard tone-mapping
	color.rgb = LinearToSRGB(color.rgb); // Gamma correction

	out_Color = uboDynamic.colorMultiplier * color;
}
