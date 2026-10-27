#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec4 in_Colour;
layout (location = 2) in vec3 in_Normal;
layout (location = 3) in vec4 in_ExtraVec4; // Procedural texture weights (grass, layered rock, granular, snow)

layout (location = 0) out vec4 ex_Colour;
layout (location = 1) out vec3 ex_NormalWS;
layout (location = 2) out float ex_Depth;
layout (location = 3) out vec3 ex_PositionWS;
layout (location = 4) out vec4 ex_Weights;

layout (binding = 0) uniform UBOConstant
{
	vec4 renderOrigin; // xyz: see Renderer::RenderOriginFor
	mat4 view;
	mat4 viewProjection;
} uboConstant;

void main()
{
	ex_Colour = in_Colour;
	ex_NormalWS = in_Normal;
	ex_Weights = in_ExtraVec4;

    // Vertices are in world space, everything else in render space (relative to renderOrigin, near the camera). The
    // subtraction is exact since both are close (or the origin is zero).
    vec4 posWS = vec4(in_Position - uboConstant.renderOrigin.xyz, 1.0);
    ex_PositionWS = posWS.xyz;
	ex_Depth = (uboConstant.view * posWS).z;
    gl_Position = uboConstant.viewProjection * posWS;
}
