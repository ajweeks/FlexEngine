#version 400

out vec4 FragColor;
in vec3 WorldPos;

uniform samplerCube cubemapSampler;

void main()
{		
    //vec3 envColor = texture(cubemapSampler, WorldPos).rgb;
    // TODO: Make LOD value a uniform
    vec3 envColor = textureLod(cubemapSampler, WorldPos, 1.2).rgb;
    
    FragColor = vec4(envColor, 1.0);
}
