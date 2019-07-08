#version 450

layout (binding = 0) uniform sampler2D in_Texture;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Color;

void main()
{
	ivec2 texSize = textureSize(in_Texture, 0);
	vec2 pixelSize = vec2(1.0 / texSize.x, 1.0 / texSize.y);

	vec4 color = texture(in_Texture, ex_TexCoord);

	out_Color = color;
}
