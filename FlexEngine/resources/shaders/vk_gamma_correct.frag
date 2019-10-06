#version 450

layout (binding = 0) uniform sampler2D in_Texture;

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
	vec4 color = texture(in_Texture, ex_TexCoord);

	color.rgb = LinearToSRGB(color.rgb);

	out_Color = color;
}
