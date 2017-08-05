#version 400

uniform mat4 in_Model;
uniform mat3 in_ModelInvTranspose;
uniform mat4 in_View;
uniform mat4 in_Projection;

in vec3 in_Position;
in vec4 in_Color;
in vec3 in_Normal;
in vec2 in_TexCoord;

out vec3 ex_FragPos;
out vec4 ex_Color;
out vec3 ex_Normal;
out vec2 ex_TexCoord;

void main() 
{
	mat4 MVP = in_Projection * in_View * in_Model;
	gl_Position = MVP * vec4(in_Position, 1.0);
	
	ex_FragPos = vec3(in_Model * vec4(in_Position, 1.0));
	ex_Color = in_Color;
	// Convert normal to model-space and prevent non-uniform scale issues
	ex_Normal = in_ModelInvTranspose * in_Normal;
	ex_TexCoord = in_TexCoord;
}
