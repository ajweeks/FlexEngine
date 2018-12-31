#version 400

out vec4 FragColor;
in vec3 ex_SampleDirection;

uniform bool enableCubemapSampler;
uniform samplerCube cubemapSampler;
uniform float exposure = 1.0;

void main() 
{
	if (enableCubemapSampler)
	{
		FragColor = texture(cubemapSampler, ex_SampleDirection) * exposure;
		FragColor.a = 1.0;
	}
	else
	{
		FragColor = vec4(0, 0, 0, 0);
	}
}
