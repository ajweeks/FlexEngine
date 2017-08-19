#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec4 inColor;
layout (location = 2) in mat3 inTBN;
layout (location = 5) in vec2 inTexCoord;

layout (binding = 0) uniform UBO
{
	mat4 projection;
	mat4 view;
	vec4 camPos; // Padded to 16 bytes (only 3 floats needed)
	vec4 lightDir; // Padded to 16 bytes (only 3 floats needed)
	vec4 ambientColor;
	vec4 specularColor;
} ubo;

// Updated once per object
layout (binding = 1) uniform UBOInstance
{
	mat4 model;
	mat4 modelInvTranspose;
	bool useDiffuseTexture;
	bool useNormalTexture;
	bool useSpecularTexture;
} uboInstance;

layout (binding = 2) uniform sampler2D diffuseMap;
layout (binding = 3) uniform sampler2D normalMap;
layout (binding = 4) uniform sampler2D specularMap;

layout (location = 0) out vec4 fragmentColor;

void main()
{
	vec3 normal;
	if (uboInstance.useNormalTexture)
	{
		vec4 normalSample = texture(normalMap, inTexCoord);
		normal = inTBN * (normalSample.xyz * 2 - 1);
	}
	else
	{
		normal = inTBN[2];
	}
	
	float lightIntensity = max(dot(normal, normalize(ubo.lightDir.xyz)), 0.0);
	lightIntensity = lightIntensity * 0.75 + 0.25;

	vec4 diffuse = lightIntensity * inColor;
	if (uboInstance.useDiffuseTexture)
	{
		vec4 diffuseSample = texture(diffuseMap, inTexCoord);
		diffuse *= diffuseSample;
	}

	float specularStrength = 0.5f;
	float shininess = 32;
	
	vec3 viewDir = normalize(ubo.camPos.xyz - inWorldPos);
	vec3 reflectDir = reflect(-normalize(ubo.lightDir.xyz), normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
	
	vec4 specular = specularStrength * spec * ubo.specularColor;
	if (uboInstance.useSpecularTexture)
	{
		vec4 specularSample = texture(specularMap, inTexCoord);
		specular *= specularSample;
	}

	fragmentColor = ubo.ambientColor + diffuse + specular;
	
	// visualize diffuse lighting:
	//fragmentColor = vec4(vec3(lightIntensity), 1); return;
	
	// visualize normals:
	//fragmentColor = vec4(normal * 0.5 + 0.5, 1); return;
	
	// visualize specular:
	//fragmentColor = specular; return;

	// visualize tex coords:
	//fragmentColor = vec4(ex_TexCoord.xy, 0, 1); return;
	
	// no lighting:
	//fragmentColor = ex_Color; return;	
}