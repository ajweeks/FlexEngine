#version 400

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 2) in vec3 in_Normal;

out vec3 ex_FragPos;
out vec2 ex_TexCoord;
out vec3 ex_Normal;

uniform mat4 in_Model;
uniform mat4 in_ViewProjection;

void main()
{
    vec4 worldPos = in_Model * vec4(in_Position, 1.0);
    ex_FragPos = worldPos.xyz; 
    
    mat3 normalMat = transpose(inverse(mat3(in_Model)));
    ex_Normal = normalMat * in_Normal;

    gl_Position = in_ViewProjection * worldPos;

	// Convert normal to model-space and prevent non-uniform scale issues
	//ex_TBN = mat3(
	//	normalize(mat3(in_ModelInvTranspose) * in_Tangent), 
	//	normalize(mat3(in_ModelInvTranspose) * in_Bitangent), 
	//	normalize(mat3(in_ModelInvTranspose) * in_Normal));

	ex_TexCoord = in_TexCoord;
}
