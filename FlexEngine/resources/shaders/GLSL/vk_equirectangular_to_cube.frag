#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) out vec4 FragColor;

layout (location = 0) in vec3 ex_SampleDirection;

layout (binding = 0) uniform sampler2D hdrEquirectangularSampler;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main()
{
    vec2 uv = SampleSphericalMap(normalize(ex_SampleDirection));
    vec3 color = texture(hdrEquirectangularSampler, uv).rgb;
	
    FragColor = vec4(color, 1.0);
}
