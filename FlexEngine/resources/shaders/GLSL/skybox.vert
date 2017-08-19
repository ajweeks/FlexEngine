#version 400

uniform mat4 in_ViewProjection;
uniform mat4 in_Model;

layout (location = 0) in vec3 in_Position;

out vec3 ex_TexCoord;

void main() 
{
	ex_TexCoord = in_Position;
	vec4 pos = in_ViewProjection * in_Model * vec4(in_Position, 1.0);
	gl_Position = pos.xyww; // Clip coords to NDC to ensure skybox is rendered at far plane
}
