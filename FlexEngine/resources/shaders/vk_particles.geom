#version 450

layout(points, invocations = 1) in;
layout(triangle_strip, max_vertices = 4) out;

layout (location = 0) in VSO
{
    vec3 position;
    vec4 color;
    float size;
} inputs[];

layout (location = 0) out GSO
{
	vec4 color;
	vec2 texCoord;
} outputs;

layout (binding = 0) uniform UBOConstant
{
	vec3 camPos;
	mat4 viewProj;
} uboConstant;

void main()
{
	vec3 pos = inputs[0].position;
	vec4 col = inputs[0].color;
	float size = inputs[0].size;
	vec3 normal = normalize(uboConstant.camPos - pos);
	vec3 bitangent = vec3(0.0, 1.0, 0.0);
	vec3 tangent = normalize(cross(bitangent, normal));
	bitangent = normalize(cross(tangent, normal));

	gl_Position = uboConstant.viewProj * vec4(pos + (bitangent * size), 1.0);
	outputs.color = col;
	outputs.texCoord = vec2(0.0, 1.0);
	EmitVertex();
	
	gl_Position = uboConstant.viewProj * vec4(pos + (tangent * size) + (bitangent * size), 1.0);
	outputs.color = col;
	outputs.texCoord = vec2(1.0);
	EmitVertex();
	
	gl_Position = uboConstant.viewProj * vec4(pos, 1.0);
	outputs.color = col;
	outputs.texCoord = vec2(0.0);
	EmitVertex();
	
	gl_Position = uboConstant.viewProj * vec4(pos + (tangent * size), 1.0);
	outputs.color = col;
	outputs.texCoord = vec2(1.0, 0.0);
	EmitVertex();
	
	EndPrimitive();
}