#version 400

out vec4 FragColor;
in vec3 ex_SampleDirection;

uniform bool useCubemapSampler;
uniform samplerCube cubemapSampler;

void main() 
{
	if (useCubemapSampler)
	{
		FragColor = texture(cubemapSampler, ex_SampleDirection);
	}
	else
	{
		FragColor = vec4(0, 0, 0, 0);
	}
}
