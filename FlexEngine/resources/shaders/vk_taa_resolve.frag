#version 420

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Color;

layout (binding = 0) uniform UBOConstant
{
	mat4 invView;
	mat4 invProj;
	mat4 lastFrameViewProj;
} uboConstant;

layout (binding = 1) uniform sampler2D in_DepthBuffer;
layout (binding = 2) uniform sampler2D in_SceneTexture;
layout (binding = 3) uniform sampler2D in_HistoryTexture;

vec3 ReconstructWSPosFromDepth(vec2 uv, float depth)
{
	float x = uv.x * 2.0 - 1.0;
	float y = (1.0 - uv.y) * 2.0 - 1.0;
	vec4 pos = vec4(x, y, depth, 1.0);
	vec4 posVS = uboConstant.invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return (uboConstant.invView * vec4(posNDC, 1)).xyz;
}

void main()
{
	float nonLinearDepth = texture(in_DepthBuffer, ex_TexCoord).r;
	vec3 wsPos = ReconstructWSPosFromDepth(ex_TexCoord, nonLinearDepth);

	vec4 lastFrameHCPos = uboConstant.lastFrameViewProj * vec4(wsPos, 1.0);
	lastFrameHCPos.xyz /= lastFrameHCPos.w;
	lastFrameHCPos.y = -lastFrameHCPos.y;

	vec2 historyUV = lastFrameHCPos.xy * 0.5 + 0.5;

	vec4 sceneCol = texture(in_SceneTexture, ex_TexCoord);
	vec4 historyCol = texture(in_HistoryTexture, historyUV);

	out_Color = mix(historyCol, sceneCol, 1/8.0);
}
