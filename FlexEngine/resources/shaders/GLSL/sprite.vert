#version 450

uniform mat3 transformMat;

in vec2 in_Position2D;
in vec2 in_TexCoord;
in vec4 in_Color;

out vec2 ex_TexCoord;
out vec4 ex_Color;

void main()
{
	ex_TexCoord = in_TexCoord;
	ex_Color = in_Color;
	vec2 transformedPos = (vec3(in_Position2D, 0) * transformMat).xy;
	gl_Position = vec4(transformedPos, 0, 1);
}