#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct DirectionalLight 
{
	vec3 direction;

	vec3 ambientCol;
	vec3 diffuseCol;
	vec3 specularCol;
};

struct PointLight 
{
	vec3 position;

	float constant;
	float linear;
	float quadratic;

	vec3 ambientCol;
	vec3 diffuseCol;
	vec3 specularCol;
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
	// Check if this light is disabled
	if (dirLightDir.x == 0.0 && dirLightDir.y == 0.0 && dirLightDir.z == 0.0) return vec3(0, 0, 0);

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
	// Check if this light is disabled
	if (plConst == 0.0f) return vec3(0, 0, 0);

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

	float specStrength = 0.5;
	float specShininess = 32.0;
	
	vec3 viewDir = normalize(ubo.camPos.xyz - worldPos);

	vec3 result = DoDirectionalLighting(ubo.dirLight.direction, ubo.dirLight.ambientCol, ubo.dirLight.diffuseCol, 
										ubo.dirLight.specularCol, diffuse, specular, normal, viewDir, specStrength, specShininess);

	for (int i = 0; i < NUMBER_POINT_LIGHTS; ++i)
	{
		result += DoPointLighting(ubo.pointLights[i].constant, ubo.pointLights[i].linear, ubo.pointLights[i].quadratic, ubo.pointLights[i].position, 
			ubo.pointLights[i].ambientCol, ubo.pointLights[i].diffuseCol, ubo.pointLights[i].specularCol, diffuse, specular, normal, worldPos, viewDir, specStrength, specShininess);
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
