#version 400

uniform vec3 lightDir;
uniform vec4 ambientColor;
uniform vec4 specularColor;
uniform vec3 camPos;
uniform sampler2D in_Texture;

in vec3 ex_WorldPos;
in vec4 ex_Color;
in vec3 ex_Normal;
in vec2 ex_TexCoords;

out vec4 fragmentColor;

void main() 
{
	vec3 normal = normalize(ex_Normal);
	float lightIntensity = max(dot(normal, normalize(lightDir)), 0.0);
	lightIntensity = lightIntensity * 0.75 + 0.25;

	vec4 textureSample = texture(in_Texture, ex_TexCoords);
	vec4 diffuse = lightIntensity * textureSample * ex_Color;
	
	float specularStrength = 0.5f;
	float shininess = 32;
	
	vec3 viewDir = normalize(camPos - ex_WorldPos);
	vec3 reflectDir = reflect(-normalize(lightDir), normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
	
	vec4 specular = specularStrength * spec * specularColor;
	
	fragmentColor = ambientColor + diffuse + specular;
	
	// No lighting:
	//fragmentColor = ex_Color;

	// Visualize tex coords:
	//fragmentColor = vec4(ex_TexCoord.xy, 0, 1);
	
	// Visualize normals:
	//fragmentColor = vec4(normal.xyz * 0.5 + 0.5, 1);
}
