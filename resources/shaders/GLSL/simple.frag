#version 400

uniform vec3 lightDir = vec3(0.5, 1.0, 1.0);
uniform vec4 ambient = vec4(0.02, 0.03, 0.025, 1.0);
uniform vec4 specularColor = vec4(1.0, 1.0, 1.0, 1.0);

in vec3 ex_FragPos;
in vec4 ex_Color;
in vec3 ex_Normal;
in vec2 ex_TexCoord;

uniform vec3 viewPos;

out vec4 fragmentColor;

void main() 
{
	vec3 normal = normalize(ex_Normal);
	float lightIntensity = max(dot(normal, -normalize(lightDir)), 0.0);
	lightIntensity = lightIntensity * 0.75 + 0.25;

	vec4 diffuse = lightIntensity * ex_Color;
	
	float specularStrength = 0.5f;
	float shininess = 32;
	
	vec3 viewDir = normalize(viewPos - ex_FragPos);
	vec3 reflectDir = reflect(-normalize(lightDir), normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
	
	vec4 specular = specularStrength * spec * specularColor;
	
	fragmentColor = ambient + diffuse + specular;
	fragmentColor = ambient + vec4(ex_Normal.xyz, 1.0) + specular;
	
	// No lighting:
	//fragmentColor = ex_Color;

	// Visualize tex coords:
	//fragmentColor = lightIntensity * vec4(ex_TexCoord.x, ex_TexCoord.y, 0, 1);
	
	// Visualize normals:
	//fragmentColor = vec4(ex_Normal.xyz * 0.5 + 0.5, 1);
}
