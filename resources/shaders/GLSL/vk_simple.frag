#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

//uniform vec3 lightDir = vec3(0.5, 1.0, 1.0);

layout (location = 0) in vec4 inColor;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inTexCoord;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	vec3 lightDir = vec3(0.5, 1.0, 1.0);
	float lightIntensity = max(dot(inNormal, -normalize(lightDir)), 0.0);
	lightIntensity = lightIntensity * 0.75 + 0.25;

	//outFragColor = lightIntensity * inColor;	
	outFragColor = lightIntensity * vec4(inTexCoord.x, inTexCoord.y, 0, 0);	
}