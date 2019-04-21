#version 400

// Deferred PBR

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec4 in_Color;
layout (location = 2) in vec2 in_TexCoord;
layout (location = 3) in vec3 in_Tangent;
layout (location = 4) in vec3 in_Bitangent;
layout (location = 5) in vec3 in_Normal;

out vec3 ex_WorldPos;
out mat3 ex_TBN;
out vec2 ex_TexCoord;
out vec4 ex_Color;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec4 worldPos = model * vec4(in_Position, 1.0);
    ex_WorldPos = worldPos.xyz; 
	
	ex_TexCoord = in_TexCoord;

	ex_Color = in_Color;

	mat3 modelRot = mat3(model);
	ex_TBN = mat3(
		normalize(modelRot * in_Tangent), 
		normalize(modelRot * in_Bitangent), 
		normalize(modelRot * in_Normal));

    gl_Position = projection * view * worldPos;
}
