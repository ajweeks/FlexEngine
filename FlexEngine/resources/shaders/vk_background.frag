#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 WorldPos;

layout (location = 0) out vec4 FragColor;

layout (binding = 0) uniform samplerCube cubemapSampler;

void main()
{		
    vec3 envColor = texture(cubemapSampler, WorldPos).rgb;
    // TODO: Make this a uniform
    //vec3 envColor = textureLod(cubemapSampler, WorldPos, 1.0).rgb;
    
    envColor = envColor / (envColor + vec3(1.0)); // HDR tonemapping
    envColor = pow(envColor, vec3(1.0 / 2.2)); // Gamma correction
    
    FragColor = vec4(envColor, 1.0);
}
