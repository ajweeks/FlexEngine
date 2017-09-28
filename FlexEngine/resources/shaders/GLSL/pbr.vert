#version 400

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 1) in vec3 in_Tangent;
layout (location = 2) in vec3 in_Bitangent;
layout (location = 3) in vec3 in_Normal;

out vec3 ex_WorldPos;
out vec2 ex_TexCoord;
// out vec3 ex_Albedo;
// out float ex_Roughness;
// out float ex_Metalic;
// out float ex_AO;
out mat3 ex_TBN;

uniform mat4 in_Model;
uniform mat4 in_ModelInvTranspose;
uniform mat4 in_ViewProjection;

void main()
{
    vec4 worldPos = in_Model * vec4(in_Position, 1.0);
    ex_WorldPos = worldPos.xyz; 
	
	ex_TexCoord = in_TexCoord;

	// ex_Albedo = in_Albedo;

	// ex_Roughness = in_RoughnessMetallicAO.x;
	// ex_Metalic = in_RoughnessMetallicAO.y;
	// ex_AO = in_RoughnessMetallicAO.z;
    
   	// Convert normal to model-space and prevent non-uniform scale issues
	ex_TBN = mat3(
		normalize(mat3(in_ModelInvTranspose) * in_Tangent), 
		normalize(mat3(in_ModelInvTranspose) * in_Bitangent), 
		normalize(mat3(in_ModelInvTranspose) * in_Normal));

    gl_Position = in_ViewProjection * worldPos;
}