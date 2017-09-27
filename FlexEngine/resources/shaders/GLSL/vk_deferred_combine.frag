#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// NOTE: All vec3s have been padded to vec4s to prevent uncertainties

struct DirectionalLight 
{
	vec4 direction;

	vec4 ambientCol;
	vec4 diffuseCol;
	vec4 specularCol;

	bool enabled;
	float padding[3];
};

struct PointLight 
{
	vec4 position;

	// Only first three floats are used (last is padding)
	vec4 constantLinearQuadraticPadding;

	vec4 ambientCol;
	vec4 diffuseCol;
	vec4 specularCol;

	bool enabled;
	float padding[3];
};
#define NUMBER_POINT_LIGHTS 4

layout (binding = 0) uniform UBOConstant
{
	vec4 camPos;
	DirectionalLight dirLight;
	PointLight pointLights[NUMBER_POINT_LIGHTS];
} ubo;

layout (binding = 1) uniform sampler2D in_PositionSampler;
layout (binding = 2) uniform sampler2D in_NormalSampler;
layout (binding = 3) uniform sampler2D in_DiffuseSpecularSampler;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 fragmentColor;

// For some reason passing in the whole DirectionalLight struct causes errors when compiling to spir-v, look into later
vec3 DoDirectionalLighting(vec3 dirLightDir, vec3 dirLightAmbient, vec3 dirLightDiffuse, vec3 dirLightSpecular, vec3 diffuseSample, float specularSample, vec3 normal, vec3 viewDir, float specStrength, float specShininess)
{
	vec3 lightDir = normalize(-dirLightDir);

	float diffuseIntensity = max(dot(normal, lightDir), 0.0);

	vec3 reflectDir = reflect(-lightDir, normal);
	float specularIntensity = pow(max(dot(viewDir, reflectDir), 0.0), specShininess);
	
	float specularCol = specStrength * specularIntensity * specularSample;

	vec3 ambient = dirLightAmbient * diffuseSample;
	vec3 diffuse = dirLightDiffuse * diffuseSample * diffuseIntensity;
	vec3 specular = dirLightSpecular * specularCol * specularIntensity;

	return (ambient + diffuse + specular);
}

vec3 DoPointLighting(float plConst, float plLin, float plQuad, vec3 plPos, vec3 plAmbient, vec3 plDiffuse, vec3 plSpecular, vec3 diffuseSample,
					 float specularSample, vec3 normal, vec3 worldPos, vec3 viewDir, float specStrength, float specShininess)
{
	vec3 lightDir = normalize(plPos - worldPos);

	float diffuseIntensity = max(dot(normal, lightDir), 0.0);

	vec3 reflectDir = reflect(-lightDir, normal);
	float specularIntensity = pow(max(dot(viewDir, reflectDir), 0.0), specShininess);

	float specularCol = specStrength * specularIntensity * specularSample;

	float distance = length(plPos - worldPos);
	float attenuation = 1.0 / (plConst + plLin * distance + plQuad * (distance * distance)); 

	vec3 ambient = plAmbient * diffuseSample * attenuation;
	vec3 diffuse = plDiffuse * diffuseSample * diffuseIntensity * attenuation;
	vec3 specular = plSpecular * specularCol * specularIntensity * attenuation;

	return (ambient + diffuse + specular);
}

void main()
{
    // retrieve data from gbuffer
    vec3 worldPos = texture(in_PositionSampler, ex_TexCoord).rgb;
    vec3 normal = texture(in_NormalSampler, ex_TexCoord).rgb;
    vec3 diffuse = texture(in_DiffuseSpecularSampler, ex_TexCoord).rgb;
    float specular = texture(in_DiffuseSpecularSampler, ex_TexCoord).a;

	//fragmentColor = vec4(normal, 1);
	//return;

	float specStrength = 0.5;
	float specShininess = 32.0;
	
	vec3 viewDir = normalize(ubo.camPos.xyz - worldPos);

	vec3 result = vec3(0);

	if (ubo.dirLight.enabled) result += DoDirectionalLighting(ubo.dirLight.direction.xyz, ubo.dirLight.ambientCol.rgb, ubo.dirLight.diffuseCol.rgb, 
										ubo.dirLight.specularCol.rgb, diffuse, specular, normal, viewDir, specStrength, specShininess);

	for (int i = 0; i < NUMBER_POINT_LIGHTS; ++i)
	{
		if (ubo.pointLights[i].enabled)
		{
			result += DoPointLighting(ubo.pointLights[i].constantLinearQuadraticPadding.x, ubo.pointLights[i].constantLinearQuadraticPadding.y, 
				ubo.pointLights[i].constantLinearQuadraticPadding.z, ubo.pointLights[i].position.xyz, ubo.pointLights[i].ambientCol.rgb, 
				ubo.pointLights[i].diffuseCol.rgb, ubo.pointLights[i].specularCol.rgb, diffuse, specular, normal, worldPos, viewDir, specStrength, specShininess);
		}
	}
	
	fragmentColor = vec4(result, 1.0);
	
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
