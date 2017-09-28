#version 400

struct DirectionalLight 
{
	vec4 direction;

	vec4 color;

	bool enabled;
	float padding[3];
};
uniform DirectionalLight dirLight;

struct PointLight 
{
	vec4 position;

	vec4 color;

	bool enabled;
	float padding[3];
};
#define NUMBER_POINT_LIGHTS 4
uniform PointLight pointLights[NUMBER_POINT_LIGHTS];

uniform sampler2D in_PositionSampler;
uniform sampler2D in_NormalSampler;
uniform sampler2D in_DiffuseSpecularSampler;

uniform vec4 in_CamPos;

in vec2 ex_TexCoord;

out vec4 fragmentColor;

vec3 DoDirectionalLighting(DirectionalLight dirLight, vec3 diffuseSample, float specularSample, vec3 normal, vec3 viewDir, float specStrength, float specShininess)
{
	vec3 lightDir = normalize(-dirLight.direction.xyz);

	float diffuseIntensity = max(dot(normal, lightDir), 0.0);

	vec3 reflectDir = reflect(-lightDir, normal);
	float specularIntensity = pow(max(dot(viewDir, reflectDir), 0.0), specShininess);
	
	float specularCol = specStrength * specularIntensity * specularSample;

	vec3 diffuse = dirLight.color.rgb * diffuseSample * diffuseIntensity;
	vec3 specular = vec3(specularCol * specularIntensity);

	return (diffuse + specular);
}

vec3 DoPointLighting(PointLight pointLight, vec3 diffuseSample, float specularSample, vec3 normal, vec3 worldPos, vec3 viewDir, float specStrength, float specShininess)
{
	vec3 lightDir = normalize(pointLight.position.xyz - worldPos);

	float diffuseIntensity = max(dot(normal, lightDir), 0.0);

	vec3 reflectDir = reflect(-lightDir, normal);
	float specularIntensity = pow(max(dot(viewDir, reflectDir), 0.0), specShininess);

	float specularCol = specStrength * specularIntensity * specularSample;

	float distance = length(pointLight.position.xyz - worldPos);
	float attenuation = 1.0 / (distance * distance); 

	vec3 diffuse = pointLight.color.rgb * diffuseSample * diffuseIntensity * attenuation;
	vec3 specular = vec3(specularCol * specularIntensity * attenuation);

	return (diffuse + specular);
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
	
	vec3 viewDir = normalize(in_CamPos.xyz - worldPos);

	vec3 result = vec3(0.0);
	if (dirLight.enabled) result += DoDirectionalLighting(dirLight, diffuse, specular, normal, viewDir, specStrength, specShininess);

	for (int i = 0; i < NUMBER_POINT_LIGHTS; ++i)
	{
		if (pointLights[i].enabled)
		{
			result += DoPointLighting(pointLights[i], diffuse, specular, normal, worldPos, viewDir, specStrength, specShininess);
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
