#version 450

// Final copy of the graded scene. Shaders already write display (sRGB encoded) values (see vk_misc.glsl), so this
// must not encode them again (doing so, after a second tone map in post processing, washed out the whole image).

layout (binding = 0) uniform sampler2D in_Texture;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Colour;

void main()
{
	out_Colour = texture(in_Texture, ex_TexCoord);
}
