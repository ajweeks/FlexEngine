#version 400

uniform mat4 in_Model;
uniform mat4 in_ModelInvTranspose;
uniform mat4 in_View;
uniform mat4 in_Projection;

in vec3 in_Position;
in vec4 in_Color;
in vec3 in_Tangent;
in vec3 in_Bitangent;
in vec3 in_Normal;
in vec2 in_TexCoord;

out vec3 ex_WorldPos;
out vec4 ex_Color;
out mat3 ex_TBN;
out vec2 ex_TexCoord;

void main()
{
	mat4 MVP = in_Projection * in_View * in_Model;
	gl_Position = MVP * vec4(in_Position, 1.0);
	
	ex_WorldPos = vec3(in_Model * vec4(in_Position, 1.0));
	ex_Color = in_Color;
	
	// Convert normal to model-space and prevent non-uniform scale issues
	ex_TBN = mat3(
		normalize(mat3(in_ModelInvTranspose) * in_Tangent), 
		normalize(mat3(in_ModelInvTranspose) * in_Bitangent), 
		normalize(mat3(in_ModelInvTranspose) * in_Normal));

	ex_TexCoord = in_TexCoord;
}
