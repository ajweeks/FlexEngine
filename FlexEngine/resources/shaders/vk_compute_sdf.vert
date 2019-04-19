#version 450 

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;

layout (location = 0) out vec2 ex_TexCoord;

layout (binding = 0) uniform UBODynamic
{
	vec4 charResXYspreadZsampleDensityW;
	int texChannel;
} uboDynamic;

void main()
{
	vec2 charRes = uboDynamic.charResXYspreadZsampleDensityW.xy;
	float spread = uboDynamic.charResXYspreadZsampleDensityW.z;

	ex_TexCoord = in_TexCoord;
	vec2 adjustment = vec2(spread * 2.0) / charRes;
	ex_TexCoord -= vec2(0.5);
	ex_TexCoord *= (vec2(1.0) + adjustment);
	ex_TexCoord += vec2(0.5);
	gl_Position = vec4(in_Position, 1.0);
}