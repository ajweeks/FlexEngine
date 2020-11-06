#version 420

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Colour;

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

layout (constant_id = 1) const int TAA_SAMPLE_COUNT = 2;

vec3 ReconstructWSPosFromDepth(vec2 uv, float depth)
{
	float x = uv.x * 2.0 - 1.0;
	float y = (1.0 - uv.y) * 2.0 - 1.0;
	vec4 pos = vec4(x, y, depth, 1.0);
	vec4 posVS = uboConstant.invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return (uboConstant.invView * vec4(posNDC, 1)).xyz;
}

// vec4 DetectEdges_Colour(vec2 uv)
// {
// 	// TODO: Define elsewhere
// 	const float PIXEL_SIZE = 0.01f;
// 	const float threshold = 0.01f;

// 	vec3 weights = vec3(0.2126, 0.7152, 0.0722);
// 	// NOTE: in_HistoryTexture must be gamma-corrected! (non-sRGB)
// 	float center = dot(texture(in_HistoryTexture, uv).rgb, weights);
// 	float f = texelFetchOffset(in_HistoryTexture, ivec2(uv), 0, 1);
// 	float left = dot(.rgb, weights);
// 	float top = dot(texelFetchOffset(in_HistoryTexture, ivec2(uv), 0, 1).rgb, weights);
// 	float right = dot(texelFetchOffset(in_HistoryTexture, uv, 0, 2).rgb, weights);
// 	float bottom = dot(texelFetchOffset(in_HistoryTexture, uv, 0, 3).rgb, weights);


// 	vec4 delta = abs(center.xxxx - vec4(left, top, right, bottom));
// 	vec4 edges = step(threshold.xxxx, delta);

// 	if (dot(edges, vec4(1.0)) == 0.0) discard;

// 	return edges;
// }

void main()
{
	ivec2 texSize = textureSize(in_SceneTexture, 0);
	vec2 pixelSize = 1.0 / texSize;

	vec2 uv = ex_TexCoord;

	float nonLinearDepth = texture(in_DepthBuffer, uv).r;
	vec3 posWS = ReconstructWSPosFromDepth(uv, nonLinearDepth);

	vec4 lastFrameHCPos = uboConstant.lastFrameViewProj * vec4(posWS, 1.0);
	lastFrameHCPos.xyz /= lastFrameHCPos.w;
	lastFrameHCPos.y = -lastFrameHCPos.y;
	vec2 historyUV = lastFrameHCPos.xy * 0.5 + 0.5;

	float alpha = 1.0 / TAA_SAMPLE_COUNT;

	vec3 sceneCol = texture(in_SceneTexture, uv).rgb;
	vec3 historyCol = texture(in_HistoryTexture, historyUV).rgb;

	float nonLinearDepthP = texture(in_DepthBuffer, historyUV).r;

	if (nonLinearDepthP < nonLinearDepth || // Disocclusion
		(nonLinearDepth < 1e-7) || (nonLinearDepthP < 1e-7)) // Hit skybox
	{
		alpha = 1.0;
	}

#if 0
	if ((nonLinearDepth < 1e-7 && nonLinearDepthP > 1e-7))
	{
		out_Colour = vec4(1.0, 0.0, 0.0, 1.0);
		return;
	}

	if ((nonLinearDepth > 1e-7 && nonLinearDepthP < 1e-7))
	{
		out_Colour = vec4(0.0, 1.0, 0.0, 1.0);
		return;
	}
#endif

	// vec3 cTL = texture(in_SceneTexture, ex_TexCoord + vec2(0, pixelSize.y)).rgb;
	// vec3 cTR = texture(in_SceneTexture, ex_TexCoord + vec2(pixelSize.x, pixelSize.y)).rgb;
	// vec3 cBL = texture(in_SceneTexture, ex_TexCoord + vec2(0, 0)).rgb;
	// vec3 cBR = texture(in_SceneTexture, ex_TexCoord + vec2(pixelSize.x, 0)).rgb;

	// vec3 cMin = min(cTL, min(cTR, min(cBL, cBR)));
	// vec3 cMax = max(cTL, max(cTR, max(cBL, cBR)));

	// Ignore samples outside the frame buffer
	if (historyUV.x > 1.0 || historyUV.x < 0.0 || historyUV.y > 1.0 || historyUV.y < 0.0)
	{
		alpha = 1.0;
	}

	// vec3 wk = abs((cTL+cTR+cBL+cBR)*0.25-sceneCol);

	out_Colour = vec4(mix(historyCol, sceneCol, alpha), 1.0);
	
	// vec3 w = clamp(1.0/(mix(vec3(pushConstants.kl), vec3(pushConstants.kh), wk)), 0.0, 1.0);
	// out_Colour = vec4(mix(clamp(historyCol, cMin, cMax), sceneCol, w), 1.0);
	//out_Colour = vec4(w, 1.0);
	
	//out_Colour = vec4(mix(clamp(historyCol, cMin, cMax), sceneCol, alpha), 1.0);
	// out_Colour = vec4(vec3(alpha), 1.0);
	
	// float range = (1.0 - initialWeight);
	// out_Colour = mix(out_Colour, vec4(1.0, 0.0, 0.0, 1.0), (alpha - initialWeight) / range);

	// vec3 diff = abs(newSceneCol - sceneCol);
	// out_Colour = mix(out_Colour, vec4(1.0, 0.0, 0.0, 1.0), diff.x+diff.y+diff.z);

	// out_Colour = mix(out_Colour, vec4(1, 0, 0, 1), clamp(length(w), 0, 1));
}
