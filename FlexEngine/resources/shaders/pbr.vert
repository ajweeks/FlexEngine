#version 450

// Deferred PBR

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 2) in vec4 in_Color;
layout (location = 3) in vec3 in_Normal;
layout (location = 4) in vec3 in_Tangent;

layout (location = 0) out vec2 ex_TexCoord;
layout (location = 1) out vec4 ex_Color;
layout (location = 2) out mat3 ex_TBN;

uniform mat4 model;
uniform mat4 viewProjection;

void main()
{
	ex_TexCoord = in_TexCoord;

	ex_Color = in_Color;

	vec3 bitan = cross(in_Normal, in_Tangent);
	ex_TBN = mat3(
		normalize(mat3(model) * in_Tangent), 
		normalize(mat3(model) * bitan), 
		normalize(mat3(model) * in_Normal));

    vec4 worldPos = model * vec4(in_Position, 1.0);
    gl_Position = viewProjection * worldPos;
}
