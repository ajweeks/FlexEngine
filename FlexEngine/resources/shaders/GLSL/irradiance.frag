#version 400

out vec4 FragColor;

in vec3 ex_SampleDirection;

uniform samplerCube cubemapSampler;

const float PI = 3.1415926536;

void main()
{		
    // The sample direction equals the hemisphere's orientation 
    vec3 normal = normalize(ex_SampleDirection);
  
    vec3 irradiance = vec3(0.0);

	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 right = cross(up, normal);
	up = cross(normal, right);

	float sampleDelta = 0.025;
	float nrSamples = 0.0; 
	for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
	{
	    for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
	    {
	        // Spherical to cartesian (in tangent space)
	        vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
	        // Tangent space to world
	        vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal; 

	        irradiance += texture(cubemapSampler, sampleVec).rgb * cos(theta) * sin(theta);
	        ++nrSamples;
	    }
	}
	irradiance = PI * irradiance * (1.0 / nrSamples);
  
    FragColor = vec4(irradiance, 1.0);
}
