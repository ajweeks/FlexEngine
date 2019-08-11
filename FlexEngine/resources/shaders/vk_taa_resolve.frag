#version 420

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Color;

layout (binding = 0) uniform UBOConstant
{
	mat4 invView;
	mat4 invProj;
	mat4 lastFrameViewProj;
	float maxUVDist;
} uboConstant;

layout (binding = 1) uniform sampler2D in_DepthBuffer;
layout (binding = 2) uniform sampler2D in_SceneTexture;
layout (binding = 3) uniform sampler2D in_HistoryTexture;

layout (constant_id = 0) const int TAA_SAMPLE_COUNT = 8;

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

	vec4 lastFrameHCPos = uboConstant.lastFrameViewProj * vec4(wsPos, 1.0);
	lastFrameHCPos.xyz /= lastFrameHCPos.w;
	lastFrameHCPos.y = -lastFrameHCPos.y;

	vec2 historyUV = lastFrameHCPos.xy * 0.5 + 0.5;

	float alpha = 1.0 / TAA_SAMPLE_COUNT;

	// Blend based on distance moved
	float dUV = clamp((length((historyUV) - (ex_TexCoord)) - 1.0/texSize.x) / uboConstant.maxUVDist, 0.0, 1.0);
	// alpha = mix(alpha, 1.0, dUV);

	// if (historyUV.x - ex_TexCoord.x > 2.0) historyUV.x = ex_TexCoord.x + 2.0;
	// else if (historyUV.x - ex_TexCoord.x < -2.0) historyUV.x = ex_TexCoord.x - 2.0;

	// if (historyUV.y - ex_TexCoord.y > 2.0) historyUV.y = ex_TexCoord.y + 2.0;
	// else if (historyUV.y - ex_TexCoord.y < -2.0) historyUV.y = ex_TexCoord.y - 2.0;

	vec3 sceneCol = texture(in_SceneTexture, ex_TexCoord).rgb;
	vec3 historyCol = texture(in_HistoryTexture, historyUV).rgb;

	// Ignore samples outside the frame buffer
	if (historyUV.x > 1.0 || historyUV.x < 0.0 || historyUV.y > 1.0 || historyUV.y < 0.0)
	{
		alpha = 1.0;
	}

	// Ignore samples which differ in color too much
	// if (abs(luminence(sceneCol) - luminence(historyCol)) > 0.1) alpha = 1.0;

	out_Color = vec4(mix(historyCol, sceneCol, alpha), 1.0);
	// out_Color = vec4(abs(historyCol - sceneCol), 1.0);

	float r = dUV;
	out_Color.r = mix(out_Color.r, r, r);
}
