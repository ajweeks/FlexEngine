#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec2 inTexCoord;

layout (binding = 0) uniform UBO
{
	mat4 projection;
	mat4 view;
	vec4 camPos; // Padded to 16 bytes (only 3 floats needed)
	vec4 lightDir; // Padded to 16 bytes (only 3 floats needed)
	vec4 ambientColor;
	vec4 specularColor;
} ubo;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	vec3 normal = normalize(inNormal);
	float lightIntensity = max(dot(normal, -normalize(ubo.lightDir.xyz)), 0.0);
	lightIntensity = lightIntensity * 0.75 + 0.25;

	vec4 diffuse = lightIntensity * inColor;
	
	float specularStrength = 0.5f;
	float shininess = 32;
	
	vec3 viewDir = normalize(ubo.camPos.xyz - inPos);
	vec3 reflectDir = reflect(-normalize(ubo.lightDir.xyz), normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
	
	vec4 specular = specularStrength * spec * ubo.specularColor;
	
	outFragColor = ubo.ambientColor + diffuse + specular;
	
	//float lightIntensity = max(dot(inNormal, -normalize(ubo.lightDir.xyz)), 0.0);
	//lightIntensity = lightIntensity * 0.75 + 0.25;

	////outFragColor = lightIntensity * inColor;	
	//outFragColor = lightIntensity * vec4(inTexCoord.x, inTexCoord.y, 0, 0);	
}