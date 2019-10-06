#version 450

layout (location = 0) in vec3 in_Position;

layout (location = 0) out vec3 ex_SampleDirection;

layout (location = 0) uniform mat4 view;
layout (location = 1) uniform mat4 projection;

void main() 
{
	ex_SampleDirection = in_Position;
	// Truncate translation part of view matrix so skybox is always "around" the camera
	vec4 pos = projection * mat4(mat3(view)) * vec4(in_Position, 1.0);
	gl_Position = pos;
	gl_Position.z = 1.0e-9f;
}
