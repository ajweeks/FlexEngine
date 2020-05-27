#version 450

layout(triangles, invocations = 1) in;
layout(triangle_strip, max_vertices = 3) out;

layout (location = 0) in VSO
{
    vec3 position;
} inputs[];

layout (location = 0) out GSO
{
	vec2 barycentrics;
} outputs;

layout (binding = 0) uniform UBOConstant
{
	mat4 viewProjection;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

void main()
{
	gl_Position = uboConstant.viewProjection * vec4(inputs[0].position, 1.0);
	outputs.barycentrics = vec2(1, 0);
	EmitVertex();

	gl_Position = uboConstant.viewProjection * vec4(inputs[1].position, 1.0);
	outputs.barycentrics = vec2(0, 1);
	EmitVertex();

	gl_Position = uboConstant.viewProjection * vec4(inputs[2].position, 1.0);
	outputs.barycentrics = vec2(0, 0);
	EmitVertex();

	EndPrimitive();
}
