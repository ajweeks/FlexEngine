#version 400

uniform mat4 view;
uniform mat4 projection;

uniform mat4 model;

layout (location = 0) in vec3 in_Position;

out vec3 ex_TexCoord;

void main() 
{
	ex_TexCoord = in_Position;
	// Truncate translation part of view matrix so skybox is always "around" the camera
	vec4 pos = projection * mat4(mat3(view)) * model * vec4(in_Position, 1.0);
	gl_Position = pos.xyww; // Clip coords to NDC to ensure skybox is rendered at far plane
}
