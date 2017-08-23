#version 400

layout (std140) uniform ViewProjectionUBO
{
	mat4 in_View;
	mat4 in_Projection;
};

uniform mat4 in_Model;

layout (location = 0) in vec3 in_Position;

out vec3 ex_TexCoord;

void main() 
{
	ex_TexCoord = in_Position;
	// Truncate translation part of view matrix so skybox is always "around" the camera
	vec4 pos = in_Projection * mat4(mat3(in_View)) * in_Model * vec4(in_Position, 1.0);
	gl_Position = pos.xyww; // Clip coords to NDC to ensure skybox is rendered at far plane
}
