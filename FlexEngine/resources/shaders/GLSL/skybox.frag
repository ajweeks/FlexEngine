#version 400

in vec3 ex_TexCoord;

uniform bool in_UseCubemapTexture;
uniform samplerCube cubemap;

void main() 
{
	if (in_UseCubemapTexture)
	{
		gl_FragColor = texture(cubemap, ex_TexCoord);
	}
	else
	{
		gl_FragColor = vec4(0, 0, 0, 0);
	}
}
