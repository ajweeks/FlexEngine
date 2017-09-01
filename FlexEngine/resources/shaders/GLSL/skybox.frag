#version 400

in vec3 ex_TexCoord;

uniform bool in_UseCubemapSampler;
uniform samplerCube in_CubemapSampler;

void main() 
{
	if (in_UseCubemapSampler)
	{
		gl_FragColor = texture(in_CubemapSampler, ex_TexCoord);
	}
	else
	{
		gl_FragColor = vec4(0, 0, 0, 0);
	}
}
