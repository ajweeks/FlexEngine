#version 400

uniform vec4 in_CamPos;
uniform vec4 in_SpecularColor;
uniform bool in_UseDiffuseTexture;
uniform bool in_UseNormalTexture;
uniform bool in_UseSpecularTexture;

struct DirectionalLight 
{
	vec3 direction;

	vec3 ambientCol;
	vec3 diffuseCol;
	vec3 specularCol;
};
uniform DirectionalLight dirLight;

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
uniform PointLight pointLights[NUMBER_POINT_LIGHTS];

uniform sampler2D in_DiffuseTexture;
uniform sampler2D in_SpecularTexture;
uniform sampler2D in_NormalTexture;

in vec3 ex_WorldPos;
in vec2 ex_TexCoord;
in vec4 ex_Color;
in mat3 ex_TBN;

out vec4 fragmentColor;

vec3 DoDirectionalLighting(DirectionalLight dirLight, vec3 normal, vec3 viewDir, float specStrength, float specShininess)
{
	if (dirLight.direction == vec3(0, 0, 0)) return vec3(0, 0, 0);

	vec3 lightDir = normalize(-dirLight.direction);

	float diffuseIntensity = max(dot(normal, lightDir), 0.0);

	vec3 diffuseCol = ex_Color.rgb;
	if (in_UseDiffuseTexture)
	{
		diffuseCol *= texture(in_DiffuseTexture, ex_TexCoord).xyz;
	}

	vec3 reflectDir = reflect(-lightDir, normal);
	float specularIntensity = pow(max(dot(viewDir, reflectDir), 0.0), specShininess);
	
	vec3 specularCol = specStrength * specularIntensity * in_SpecularColor.rgb;
	if (in_UseSpecularTexture)
	{
		specularCol *= texture(in_SpecularTexture, ex_TexCoord).rgb;
	}

	vec3 ambient = dirLight.ambientCol * diffuseCol;
	vec3 diffuse = dirLight.diffuseCol * diffuseCol * diffuseIntensity;
	vec3 specular = dirLight.specularCol * specularCol * specularIntensity;

	return (ambient + diffuse + specular);
}

vec3 DoPointLighting(PointLight pointLight, vec3 normal, vec3 worldPos, vec3 viewDir, float specStrength, float specShininess)
{
	if (pointLight.constant == 0.0f) return vec3(0, 0, 0);

	vec3 lightDir = normalize(pointLight.position - worldPos);

	float diffuseIntensity = max(dot(normal, lightDir), 0.0);

	vec3 diffuseCol = ex_Color.rgb;
	if (in_UseDiffuseTexture)
	{
		diffuseCol *= texture(in_DiffuseTexture, ex_TexCoord).xyz;
	}

	vec3 reflectDir = reflect(-lightDir, normal);
	float specularIntensity = pow(max(dot(viewDir, reflectDir), 0.0), specShininess);
	
	vec3 specularCol = specStrength * specularIntensity * in_SpecularColor.rgb;
	if (in_UseSpecularTexture)
	{
		specularCol *= texture(in_SpecularTexture, ex_TexCoord).rgb;
	}

	float distance = length(pointLight.position - ex_WorldPos);
	float attenuation = 1.0 / (pointLight.constant + pointLight.linear * distance + pointLight.quadratic * (distance * distance)); 

	vec3 ambient = pointLight.ambientCol * diffuseCol * attenuation;
	vec3 diffuse = pointLight.diffuseCol * diffuseCol * diffuseIntensity * attenuation;
	vec3 specular = pointLight.specularCol * specularCol * specularIntensity * attenuation;

	return (ambient + diffuse + specular);
}

void main()
{
	vec3 normal;
	if (in_UseNormalTexture)
	{
		vec4 normalSample = texture(in_NormalTexture, ex_TexCoord);
		normal = normalize(ex_TBN * (normalSample.xyz * 2 - 1));
	}
	else
	{
		normal = normalize(ex_TBN[2]);
	}

	float specStrength = 0.5;
	float specShininess = 32.0;
	
	vec3 viewDir = normalize(in_CamPos.xyz - ex_WorldPos);

	vec3 result = DoDirectionalLighting(dirLight, normal, viewDir, specStrength, specShininess);

	for (int i = 0; i < NUMBER_POINT_LIGHTS; ++i)
	{
		result += DoPointLighting(pointLights[i], normal, ex_WorldPos, viewDir, specStrength, specShininess);
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
