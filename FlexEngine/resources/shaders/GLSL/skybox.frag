#version 400

in vec3 ex_TexCoord;

uniform bool useCubemapSampler;
uniform samplerCube cubemapSampler;

void main() 
{
	if (useCubemapSampler)
	{
		gl_FragColor = texture(cubemapSampler, ex_TexCoord);
	}
	else
	{
		gl_FragColor = vec4(0, 0, 0, 0);
	}
}
