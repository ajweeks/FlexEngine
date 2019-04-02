#version 450

layout(points, invocations = 1) in;
layout(triangle_strip, max_vertices = 4) out;

layout (location = 0) in VSO
{
    vec3 position;
    vec4 color;
    vec3 tangent;
    vec2 texCoord;
    vec2 charSizePixels;
    vec2 charSizeNorm;
	flat int channel;
} inputs[];

layout (location = 0) out GSO
{
	flat int channel;
	vec4 color;
	vec2 texCoord;
} outputs;

layout (binding = 0) uniform UBODynamic
{
	mat4 model;
	vec4 thresholdXshadowYZsoftenW;
	vec2 texSize;
} uboDynamic;

void main()
{
	vec2 charSizePixels = inputs[0].charSizePixels;
	vec2 charSizeNorm = inputs[0].charSizeNorm;

	int channel = inputs[0].channel;
	vec3 pos = inputs[0].position;
	vec2 uv = inputs[0].texCoord;
	vec4 col = inputs[0].color;
	vec3 tangent = inputs[0].tangent;
	vec3 bitangent = vec3(0, 1, 0);

	vec2 normUV = vec2(charSizePixels.x, charSizePixels.y) / uboDynamic.texSize;
	
	outputs.channel = channel;
	gl_Position = uboDynamic.model * vec4(pos + (bitangent * -charSizeNorm.y), 1.0);
	outputs.color = col;
	outputs.texCoord = uv + vec2(0, normUV.y);
	EmitVertex();
	
	outputs.channel = channel;
	gl_Position = uboDynamic.model * vec4(pos + (tangent * charSizeNorm.x) + (bitangent * -charSizeNorm.y), 1.0);
	outputs.color = col;
	outputs.texCoord = uv + normUV;
	EmitVertex();
	
	outputs.channel = channel;
	gl_Position = uboDynamic.model * vec4(pos, 1.0);
	outputs.color = col;
	outputs.texCoord = uv;
	EmitVertex();
	
	outputs.channel = channel;
	gl_Position = uboDynamic.model * vec4(pos + (tangent * charSizeNorm.x), 1.0);
	outputs.color = col;
	outputs.texCoord = uv + vec2(normUV.x, 0.0);
	EmitVertex();
	
	EndPrimitive();
}