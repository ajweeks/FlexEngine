#version 450

layout (binding = 0) uniform sampler2D in_SceneTexture;
layout (binding = 1) uniform sampler2D in_HistoryTexture;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Color;

void main()
{
	// ivec2 texSize = textureSize(in_SceneTexture, 0);
	// vec2 pixelSize = vec2(1.0 / texSize.x, 1.0 / texSize.y);

	vec4 sceneCol = texture(in_SceneTexture, ex_TexCoord);
	vec4 historyCol = texture(in_HistoryTexture, ex_TexCoord);

	out_Color = mix(sceneCol, historyCol, 1/16.0);
}
