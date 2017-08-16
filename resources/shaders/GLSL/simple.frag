#version 400

uniform vec4 in_LightDir;
uniform vec4 in_AmbientColor;
uniform vec4 in_SpecularColor;
uniform vec4 in_CamPos;

uniform sampler2D in_DiffuseMap;
uniform sampler2D in_SpecularMap;
uniform sampler2D in_NormalTexture;

uniform bool in_UseDiffuseTexture;
uniform bool in_UseNormalTexture;
uniform bool in_UseSpecularTexture;

in vec3 ex_WorldPos;
in vec4 ex_Color;
in mat3 ex_TBN;
in vec2 ex_TexCoord;

out vec4 fragmentColor;

void main()
{
	vec3 normal;
	if (in_UseNormalTexture)
	{
		vec4 normalSample = texture(in_NormalTexture, ex_TexCoord);
		normal = ex_TBN * (normalSample.xyz * 2 - 1);
	}
	else
	{
		normal = ex_TBN[2];
	}

	float lightIntensity = max(dot(normal, normalize(in_LightDir.xyz)), 0.0);
	lightIntensity = lightIntensity * 0.75 + 0.25;

	vec4 diffuse = lightIntensity * ex_Color;
	if (in_UseDiffuseTexture)
	{
		vec4 diffuseSample = texture(in_DiffuseMap, ex_TexCoord);
		diffuse *= diffuseSample;
	}
	
	float specularStrength = 0.5f;
	float shininess = 32;
	
	vec3 viewDir = normalize(in_CamPos.xyz - ex_WorldPos);
	vec3 reflectDir = reflect(-normalize(in_LightDir.xyz), normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
	
	vec4 specular = specularStrength * spec * in_SpecularColor;
	if (in_UseSpecularTexture)
	{
		vec4 specularSample = texture(in_SpecularMap, ex_TexCoord);
		specular *= specularSample;
	}

	fragmentColor = in_AmbientColor + diffuse + specular;
	
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
