#version 450

uniform mat4 in_Model;
uniform mat4 in_ViewProjection;
uniform mat4 in_ModelInverse;

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
	ex_FragPos = vec3(in_Model * vec4(in_Position, 1.0));
	mat4 MVP = in_ViewProjection * in_Model;
	ex_Normal = mat3(transpose(in_ModelInverse)) * in_Normal;
	ex_Color = in_Color;
	ex_TexCoord = in_TexCoord;
	gl_Position = MVP * vec4(in_Position, 1.0);
}
