#version 400

layout(points, invocations = 1) in;
layout(triangle_strip, max_vertices = 4) out;

in VSO
{
    vec3 position;
    vec4 color;
    vec2 texCoord;
    vec2 charSizePixels;
    vec2 charSizeNorm;
	flat int channel;
} inputs[];

out GSO
{
	flat int channel;
	vec4 color;
	vec2 texCoord;
} outputs;

uniform mat4 transformMat;
uniform vec2 texSize;

void main()
{
	vec2 charSizePixels = inputs[0].charSizePixels;
	vec2 charSizeNorm = inputs[0].charSizeNorm;

	int channel = inputs[0].channel;
	vec3 pos = inputs[0].position;
	vec2 uv = inputs[0].texCoord;
	vec4 col = inputs[0].color;

	vec2 normUV = vec2(charSizePixels.x, charSizePixels.y) / texSize;
	
	outputs.channel = channel;
	gl_Position = transformMat * vec4(pos.x, pos.y - charSizeNorm.y, pos.z, 1.0);
	outputs.color = col;
	outputs.texCoord = uv + vec2(0, normUV.y);
	EmitVertex();
	
	outputs.channel = channel;
	gl_Position = transformMat * vec4(pos.x + charSizeNorm.x, pos.y - charSizeNorm.y, pos.z, 1.0);
	outputs.color = col;
	outputs.texCoord = uv + normUV;
	EmitVertex();
	
	outputs.channel = channel;
	gl_Position = transformMat * vec4(pos.x, pos.y, pos.z, 1.0);
	outputs.color = col;
	outputs.texCoord = uv;
	EmitVertex();
	
	outputs.channel = channel;
	gl_Position = transformMat * vec4(pos.x + charSizeNorm.x, pos.y, pos.z, 1.0);
	outputs.color = col;
	outputs.texCoord = uv + vec2(normUV.x, 0.0);
	EmitVertex();
	
	EndPrimitive();
}