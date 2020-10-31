#version 450

layout (location = 0) in GSO
{
	vec2 barycentrics;
} inputs;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
	vec4 colour;
} uboDynamic;

layout (location = 0) out vec4 fragColor;

void main()
{
    vec3 bary = vec3(inputs.barycentrics.x, inputs.barycentrics.y, 1.0 - inputs.barycentrics.x - inputs.barycentrics.y);
    vec3 delta = fwidth(bary);
    vec3 edgeDist = smoothstep(vec3(0), delta*0.9,bary);
    fragColor = vec4(uboDynamic.colour.rgb, 1.0-min(edgeDist.x, min(edgeDist.y, edgeDist.z)));
}
