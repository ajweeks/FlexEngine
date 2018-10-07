#version 400

// Deferred PBR

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec4 in_Color;
layout (location = 2) in vec3 in_Tangent;
layout (location = 3) in vec3 in_Bitangent;
layout (location = 4) in vec3 in_Normal;

out vec3 ex_WorldPos;
out mat3 ex_TBN;
out vec2 ex_TexCoord;
out vec4 ex_Color;

uniform float textureScale;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec4 worldPos = model * vec4(in_Position, 1.0);
    ex_WorldPos = worldPos.xyz; 
	
	ex_TexCoord = (worldPos.xz+worldPos.y) * textureScale;

	ex_Color = in_Color;

	ex_TBN = mat3(
		normalize(mat3(model) * in_Tangent), 
		normalize(mat3(model) * in_Bitangent), 
		normalize(mat3(model) * in_Normal));

    gl_Position = projection * view * worldPos;
}
