#version 400

uniform vec3 lightDir;
uniform vec4 ambientColor;
uniform vec4 specularColor;
uniform vec3 camPos;

uniform sampler2D diffuseMap;
uniform sampler2D specularMap;
uniform sampler2D normalMap;

uniform bool useDiffuseTexture;
uniform bool useNormalTexture;
uniform bool useSpecularTexture;

in vec3 ex_WorldPos;
in vec4 ex_Color;
in mat3 ex_TBN;
in vec2 ex_TexCoord;

out vec4 fragmentColor;

void main()
{
	vec3 normal;
	if (useNormalTexture)
	{
		vec4 normalSample = texture(normalMap, ex_TexCoord);
		normal = ex_TBN * (normalSample.xyz * 2 - 1);
	}
	else
	{
		normal = ex_TBN[2];
	}

	float lightIntensity = max(dot(normal, normalize(lightDir)), 0.0);
	lightIntensity = lightIntensity * 0.75 + 0.25;

	vec4 diffuse = lightIntensity * ex_Color;
	if (useDiffuseTexture)
	{
		vec4 diffuseSample = texture(diffuseMap, ex_TexCoord);
		diffuse *= diffuseSample;
	}
	
	float specularStrength = 0.5f;
	float shininess = 32;
	
	vec3 viewDir = normalize(camPos - ex_WorldPos);
	vec3 reflectDir = reflect(-normalize(lightDir), normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
	
	vec4 specular = specularStrength * spec * specularColor;
	if (useSpecularTexture)
	{
		vec4 specularSample = texture(specularMap, ex_TexCoord);
		specular *= specularSample;
	}

	fragmentColor = ambientColor + diffuse + specular;
	
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
