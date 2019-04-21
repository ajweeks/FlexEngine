#version 400

out vec4 FragColor;
in vec3 ex_SampleDirection;

uniform bool enableCubemapSampler;
uniform samplerCube cubemapSampler;
uniform float time;
uniform float exposure = 1.0;

float noise(float s)
{
	return fract(546354.846451 * s * s + 4564.4123);
}

void main() 
{
	if (enableCubemapSampler)
	{
		FragColor = texture(cubemapSampler, ex_SampleDirection) * exposure;
		FragColor.a = 1.0;
	}
	else
	{
		vec3 dir = normalize(ex_SampleDirection);

		float n = noise(abs(ex_SampleDirection.x + ex_SampleDirection.y))*0.1; // Noise to prevent banding
		FragColor = mix(vec4(0.04, 0.03, 0.03, 1), vec4(0.01, 0.025, 0.05, 1), max(0, dir.y + 0.5 + n));
		float t = mod(time, 2.0) - 1.0;
		float tt = sin(time*2.0)+1.0;
		float d = mix(dir.x+dir.z/2.0, dir.y, tt);
		FragColor = mix(FragColor, vec4(1, 0, 0, 1), min(smoothstep(0.0f, 0.02f, d - tt), smoothstep(-0.02f, 0.0f, tt - d)));
	}
}
