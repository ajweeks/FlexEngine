#version 450

layout (location = 0) in GSO
{
	vec2 barycentrics;
} inputs;

layout (location = 0) out vec4 fragColor;

void main()
{
    vec3 bary = vec3(inputs.barycentrics.x, inputs.barycentrics.y, 1.0 - inputs.barycentrics.x - inputs.barycentrics.y);
    float edgeDist = smoothstep(0.01,0.025,min(bary.x, min(bary.y, bary.z)));
    fragColor = vec4(0.95, 0.01, 0.02, 1.0-edgeDist);
}
