#version 400

uniform vec3 lightDir;
uniform vec4 ambientColor;
uniform vec4 specularColor;
uniform vec3 camPos;

uniform sampler2D diffuseMap;
uniform sampler2D specularMap;
uniform sampler2D normalMap;

in vec3 ex_WorldPos;
in vec4 ex_Color;
in vec3 ex_Normal;
in vec2 ex_TexCoords;

out vec4 fragmentColor;

void main() 
{
	vec4 normalSample = texture(normalMap, ex_TexCoords);
	vec3 normal = normalSample.xyz * 2 - 1; //normalize(ex_Normal);
	float lightIntensity = max(dot(normal, normalize(lightDir)), 0.0);
	lightIntensity = lightIntensity * 0.75 + 0.25;

	// visualize normals:
	//fragmentColor = vec4(normal, 1); return;
	
	vec4 diffuseSample = texture(diffuseMap, ex_TexCoords);
	vec4 diffuse = lightIntensity * diffuseSample * ex_Color;
	
	vec4 specularSample = texture(specularMap, ex_TexCoords);
	float specularStrength = 0.5f;
	float shininess = 32;
	
	vec3 viewDir = normalize(camPos - ex_WorldPos);
	vec3 reflectDir = reflect(-normalize(lightDir), normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
	
	vec4 specular = specularStrength * specularSample * spec * specularColor;
	
	// visualize specular:
	//fragmentColor = specular; return;
	
	fragmentColor = ambientColor + diffuse + specular;
	
	// no lighting:
	//fragmentColor = ex_Color;

	// visualize tex coords:
	//fragmentColor = vec4(ex_TexCoord.xy, 0, 1);
}
