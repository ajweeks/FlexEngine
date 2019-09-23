#version 450

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec3 in_Normal;
layout (location = 2) in vec3 in_Tangent;

layout (location = 0) out mat3 ex_TBN;
layout (location = 3) out vec3 ex_WorldPos;

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 proj;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;

	vec4 cR0;
	vec4 cR1;
	vec4 cG0;
	vec4 cG1;
	vec4 cB0;
	vec4 cB1;
	vec4 cRGB2;
} uboDynamic;

void main()
{
	vec4 worldPos = uboDynamic.model * vec4(in_Position, 1.0);
    ex_WorldPos = worldPos.xyz;
    gl_Position = uboConstant.proj *  uboConstant.view * worldPos;

    vec3 bitan = cross(in_Normal, in_Tangent);
	ex_TBN = mat3(
		normalize(mat3(uboDynamic.model) * in_Tangent), 
		normalize(mat3(uboDynamic.model) * bitan), 
		normalize(mat3(uboDynamic.model) * in_Normal));
}
