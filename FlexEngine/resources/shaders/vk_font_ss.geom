#version 450

layout(points, invocations = 1) in;
layout(triangle_strip, max_vertices = 4) out;

layout (location = 0) in VSO
{
    vec2 position;
    vec4 color;
    vec2 texCoord;
    vec2 charSizePixels;
    vec2 charSizeNorm;
	flat int channel;
} inputs[];

layout (location = 0) out GSO
{
	vec4 color;
	vec2 texCoord;
	flat int channel;
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

	vec2 pos = inputs[0].position;
	vec2 uv = inputs[0].texCoord;
	
	vec2 normUV = charSizePixels / uboDynamic.texSize;
	
	outputs.channel = inputs[0].channel;
	gl_Position = uboDynamic.model * vec4(pos.x, pos.y - charSizeNorm.y, 0, 1);
	outputs.color = inputs[0].color;
	outputs.texCoord = uv + vec2(0, normUV.y);
	EmitVertex();
	
	outputs.channel = inputs[0].channel;
	gl_Position = uboDynamic.model * vec4(pos.x + charSizeNorm.x, pos.y - charSizeNorm.y, 0, 1);
	outputs.color = inputs[0].color;
	outputs.texCoord = uv + normUV;
	EmitVertex();
	
	outputs.channel = inputs[0].channel;
	gl_Position = uboDynamic.model * vec4(pos.x, pos.y, 0, 1);
	outputs.color = inputs[0].color;
	outputs.texCoord = uv;
	EmitVertex();
	
	outputs.channel = inputs[0].channel;
	gl_Position = uboDynamic.model * vec4(pos.x + charSizeNorm.x, pos.y, 0, 1);
	outputs.color = inputs[0].color;
	outputs.texCoord = uv + vec2(normUV.x, 0);
	EmitVertex();
	
	EndPrimitive();
}