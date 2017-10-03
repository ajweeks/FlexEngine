#version 400

out vec4 FragColor;
in vec3 WorldPos;

uniform samplerCube cubemapSampler;

void main()
{		
    vec3 envColor = texture(cubemapSampler, WorldPos).rgb;
    
    envColor = envColor / (envColor + vec3(1.0)); // HDR tonemapping
    envColor = pow(envColor, vec3(1.0 / 2.2)); // Gamma correction
    
    FragColor = vec4(envColor, 1.0);
}
