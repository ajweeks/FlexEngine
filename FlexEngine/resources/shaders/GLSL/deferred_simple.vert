#version 400

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 2) in vec3 in_Color;
layout (location = 3) in vec3 in_Tangent;
layout (location = 4) in vec3 in_Bitangent;
layout (location = 5) in vec3 in_Normal;

out vec3 ex_FragPos;
out vec2 ex_TexCoord;
out vec3 ex_Color;
out mat3 ex_TBN;

uniform mat4 model;
uniform mat4 modelInvTranspose;
uniform mat4 viewProjection;

void main()
{
    vec4 worldPos = model * vec4(in_Position, 1.0);
    ex_FragPos = worldPos.xyz; 
	
	ex_TexCoord = in_TexCoord;
    
	ex_Color = in_Color;

   	// Convert normal to model-space and prevent non-uniform scale issues
	ex_TBN = mat3(
		normalize(mat3(modelInvTranspose) * in_Tangent), 
		normalize(mat3(modelInvTranspose) * in_Bitangent), 
		normalize(mat3(modelInvTranspose) * in_Normal));

    gl_Position = viewProjection * worldPos;
}
