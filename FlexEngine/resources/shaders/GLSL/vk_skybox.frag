#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// Updated once per object
layout (binding = 1) uniform UBOInstance
{
	mat4 model;
	bool useCubemapTexture;
} uboInstance;

layout (binding = 2) uniform samplerCube cubemap;

layout (location = 0) in vec3 ex_TexCoord;

layout (location = 0) out vec4 fragmentColor;

void main()
{
	if (uboInstance.useCubemapTexture)
	{
		fragmentColor = texture(cubemap, ex_TexCoord);
	}
	else
	{
		fragmentColor = vec4(0, 0, 0, 0);
	}
}
