#version 450

// Deferred PBR Combine

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;

layout (location = 0) out vec3 ex_WorldPosition;

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 viewProjection;
} uboConstant;

// Updated once per object
layout (binding = 1) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

void main()
{
    ex_WorldPosition = in_Position;

    vec4 clipPos = uboConstant.viewProjection * uboDynamic.model * vec4(in_Position, 1.0);
	gl_Position = clipPos.xyww;
}
