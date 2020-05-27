#version 450

layout (location = 0) in vec3 in_Position;

layout (location = 0) out VSO
{
    vec3 positionWS;
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
    vec4 worldPos = uboDynamic.model * vec4(in_Position, 1.0);
    gl_Position = uboConstant.viewProjection * worldPos;
    outputs.positionWS = worldPos.xyz;
}
