#version 420

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Color;

layout (binding = 0) uniform UBOConstant
{
	mat4 invView;
	mat4 invProj;
	mat4 lastFrameViewProj;
} uboConstant;

layout (push_constant) uniform PushConstants
{
	float kl;
	float kh;
} pushConstants;

layout (binding = 1) uniform sampler2D in_DepthBuffer;
layout (binding = 2) uniform sampler2D in_SceneTexture;
layout (binding = 3) uniform sampler2D in_HistoryTexture;

layout (constant_id = 0) const int TAA_SAMPLE_COUNT = 2;

vec3 ReconstructWSPosFromDepth(vec2 uv, float depth)
{
	float x = uv.x * 2.0 - 1.0;
	float y = (1.0 - uv.y) * 2.0 - 1.0;
	vec4 pos = vec4(x, y, depth, 1.0);
	vec4 posVS = uboConstant.invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return (uboConstant.invView * vec4(posNDC, 1)).xyz;
}

float luminence(vec3 rgb)
{
	// TODO: Use proper values here
	return dot(rgb, vec3(0.3, 0.6, 0.2));
}

void main()
{
	float nonLinearDepth = texture(in_DepthBuffer, ex_TexCoord).r;
	vec3 wsPos = ReconstructWSPosFromDepth(ex_TexCoord, nonLinearDepth);
	ivec2 texSize = textureSize(in_SceneTexture, 0);
	vec2 pixelSize = 1.0 / texSize;

	vec4 lastFrameHCPos = uboConstant.lastFrameViewProj * vec4(wsPos, 1.0);
	lastFrameHCPos.xyz /= lastFrameHCPos.w;
	lastFrameHCPos.y = -lastFrameHCPos.y;

	vec2 historyUV = lastFrameHCPos.xy * 0.5 + 0.5;

	float initialWeight = 1.0 / TAA_SAMPLE_COUNT;
	float alpha = initialWeight;

	vec3 sceneCol = texture(in_SceneTexture, ex_TexCoord).rgb;
	vec3 historyCol = texture(in_HistoryTexture, historyUV).rgb;

	vec3 cTL = texture(in_SceneTexture, ex_TexCoord + vec2(0, pixelSize.y)).rgb;
	vec3 cTR = texture(in_SceneTexture, ex_TexCoord + vec2(pixelSize.x, pixelSize.y)).rgb;
	vec3 cBL = texture(in_SceneTexture, ex_TexCoord + vec2(0, 0)).rgb;
	vec3 cBR = texture(in_SceneTexture, ex_TexCoord + vec2(pixelSize.x, 0)).rgb;

	vec3 cMin = min(cTL, min(cTR, min(cBL, cBR)));
	vec3 cMax = max(cTL, max(cTR, max(cBL, cBR)));

	// Ignore samples outside the frame buffer
	if (historyUV.x > 1.0 || historyUV.x < 0.0 || historyUV.y > 1.0 || historyUV.y < 0.0)
	{
		alpha = 1.0;
	}

	vec3 wk = abs((cTL+cTR+cBL+cBR)*0.25-sceneCol);

	// out_Color = vec4(mix(historyCol, newSceneCol, alpha), 1.0);
	vec3 w = clamp(1.0/(mix(vec3(pushConstants.kl), vec3(pushConstants.kh), wk)), 0.0, 1.0);
	out_Color = vec4(mix(clamp(historyCol, cMin, cMax), sceneCol, w), 1.0);
	
	// float range = (1.0 - initialWeight);
	// out_Color = mix(out_Color, vec4(1.0, 0.0, 0.0, 1.0), (alpha - initialWeight) / range);

	// vec3 diff = abs(newSceneCol - sceneCol);
	// out_Color = mix(out_Color, vec4(1.0, 0.0, 0.0, 1.0), diff.x+diff.y+diff.z);

	// out_Color = mix(out_Color, vec4(1, 0, 0, 1), clamp(length(w), 0, 1));
}
