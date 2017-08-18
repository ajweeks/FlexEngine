#version 400

layout (std140) uniform ViewProjectionCombinedUBO
{
	mat4 in_ViewProjection;
};

uniform mat4 in_Model;

in vec3 in_Position;
in vec4 in_Color;

out vec4 ex_Color;

void main() 
{
	gl_Position = in_ViewProjection * in_Model * vec4(in_Position, 1.0);
	
	ex_Color = in_Color;
}
