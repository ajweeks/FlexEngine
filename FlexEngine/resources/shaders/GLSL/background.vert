#version 400

layout (location = 0) in vec3 in_Position;

out vec3 WorldPos;

uniform mat4 projection;
uniform mat4 view;

uniform mat4 model;

void main()
{
    WorldPos = in_Position;

    // Remove translation part from view matrix to keep it centered around viewer
	vec4 clipPos = projection * mat4(mat3(view)) * model * vec4(in_Position, 1.0);

	gl_Position = clipPos.xyww;
}
