#version 420

// Temporal anti-aliasing resolve (runs on the graded, display space scene; see Renderer::SetAntiAliasing)
// - The camera's projection is jittered every frame (BaseCamera::JitterMatrix, a Halton sequence within a pixel), so
//   accumulating frames averages each pixel's coverage.
// - Motion vectors come from the depth buffer: each pixel's position is reprojected with last frame's (unjittered)
//   view projection. The closest depth in a 3x3 neighbourhood is used so edges move with the foreground object.
// - Objects which move on their own (and transparents, which don't write depth) have no motion vectors, so the
//   history is clipped to the current neighbourhood's colour distribution (variance clipping in YCoCg) to stop them
//   ghosting. Pixels whose history was rejected get more of the current frame.

#include "vk_misc.glsl"

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
	// x: minimum history weight (fast change), y: maximum history weight (static), z: 1 to discard the history (first
	// frame, resize, camera cut), w: history weight lost per pixel of motion
	vec4 params;
} pushConstants;

layout (binding = 1) uniform sampler2D in_DepthBuffer;
layout (binding = 2) uniform sampler2D in_SceneTexture;
layout (binding = 3) uniform sampler2D in_HistoryTexture;

vec3 ReconstructRSPosFromDepth(vec2 uv, float depth)
{
	float x = uv.x * 2.0 - 1.0;
	float y = (1.0 - uv.y) * 2.0 - 1.0;
	vec4 posVS = uboConstant.invProj * vec4(x, y, depth, 1.0);
	return (uboConstant.invView * vec4(posVS.xyz / posVS.w, 1.0)).xyz;
}

vec3 RGBToYCoCg(vec3 c)
{
	return vec3(
		 0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
		 0.5  * c.r             - 0.5  * c.b,
		-0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 YCoCgToRGB(vec3 c)
{
	return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

vec3 SafeColour(vec3 c)
{
	// A NaN/inf from one bad frame (e.g. a broken mesh) would otherwise live in the history forever & spread through
	// the reprojection
	return (any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : max(c, vec3(0.0));
}

// 5 tap Catmull-Rom filter (bilinear taps at the corners skipped), which keeps the history sharp when it's resampled
// under motion (a bilinear history blurs a little more every frame)
vec3 SampleHistoryCatmullRom(vec2 uv, vec2 texSize)
{
	vec2 samplePos = uv * texSize;
	vec2 texPos1 = floor(samplePos - 0.5) + 0.5;
	vec2 f = samplePos - texPos1;

	vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	vec2 w3 = f * f * (-0.5 + 0.5 * f);

	vec2 w12 = w1 + w2;
	vec2 offset12 = w2 / w12;

	vec2 texPos0 = (texPos1 - 1.0) / texSize;
	vec2 texPos3 = (texPos1 + 2.0) / texSize;
	vec2 texPos12 = (texPos1 + offset12) / texSize;

	vec3 result =
		texture(in_HistoryTexture, vec2(texPos12.x, texPos0.y)).rgb * (w12.x * w0.y) +
		texture(in_HistoryTexture, vec2(texPos0.x, texPos12.y)).rgb * (w0.x * w12.y) +
		texture(in_HistoryTexture, vec2(texPos12.x, texPos12.y)).rgb * (w12.x * w12.y) +
		texture(in_HistoryTexture, vec2(texPos3.x, texPos12.y)).rgb * (w3.x * w12.y) +
		texture(in_HistoryTexture, vec2(texPos12.x, texPos3.y)).rgb * (w12.x * w3.y);
	float weightSum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
	return max(result / weightSum, vec3(0.0));
}

// Clips colour towards the centre of the box (rather than clamping each channel), which keeps its hue
vec3 ClipToAABB(vec3 colour, vec3 boxMin, vec3 boxMax)
{
	vec3 centre = 0.5 * (boxMax + boxMin);
	vec3 extents = max(0.5 * (boxMax - boxMin), vec3(1e-5));
	vec3 offset = colour - centre;
	vec3 ts = abs(extents / max(abs(offset), vec3(1e-5)));
	float t = clamp(min(ts.x, min(ts.y, ts.z)), 0.0, 1.0);
	return centre + offset * t;
}

void main()
{
	vec2 texSize = vec2(textureSize(in_SceneTexture, 0));
	vec2 texelSize = 1.0 / texSize;
	ivec2 pixel = ivec2(ex_TexCoord * texSize);
	ivec2 maxPixel = ivec2(texSize) - 1;
	vec2 uv = (vec2(pixel) + 0.5) * texelSize;

	// Neighbourhood: colour moments for the history clip, & the closest depth (reversed-Z: the largest) for motion
	vec3 m1 = vec3(0.0);
	vec3 m2 = vec3(0.0);
	vec3 boxMin = vec3(1e9);
	vec3 boxMax = vec3(-1e9);
	vec3 current = vec3(0.0);
	float closestDepth = -1.0;
	ivec2 closestOffset = ivec2(0);
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			ivec2 p = clamp(pixel + ivec2(x, y), ivec2(0), maxPixel);
			vec3 c = SafeColour(texelFetch(in_SceneTexture, p, 0).rgb);
			if (x == 0 && y == 0)
			{
				current = c;
			}
			vec3 cy = RGBToYCoCg(c);
			m1 += cy;
			m2 += cy * cy;
			boxMin = min(boxMin, cy);
			boxMax = max(boxMax, cy);

			// Low quality only dilates over the cross
			if (QUALITY_LEVEL >= 1 || x == 0 || y == 0)
			{
				float d = texelFetch(in_DepthBuffer, p, 0).r;
				if (d > closestDepth)
				{
					closestDepth = d;
					closestOffset = ivec2(x, y);
				}
			}
		}
	}

	// Reproject (the matrices are unjittered, so a static camera maps every pixel onto itself)
	vec2 closestUV = uv + vec2(closestOffset) * texelSize;
	vec3 posRS = ReconstructRSPosFromDepth(closestUV, closestDepth);
	vec4 lastHC = uboConstant.lastFrameViewProj * vec4(posRS, 1.0);
	vec2 lastNDC = lastHC.xy / lastHC.w;
	vec2 velocity = vec2(lastNDC.x * 0.5 + 0.5, -lastNDC.y * 0.5 + 0.5) - closestUV;
	vec2 historyUV = uv + velocity;

	bool bHistoryValid = pushConstants.params.z < 0.5 && lastHC.w > 0.0 &&
		all(greaterThanEqual(historyUV, vec2(0.0))) && all(lessThanEqual(historyUV, vec2(1.0)));
	if (!bHistoryValid)
	{
		out_Colour = vec4(current, 1.0);
		return;
	}

	vec3 history = (QUALITY_LEVEL >= 1) ? SampleHistoryCatmullRom(historyUV, texSize) : texture(in_HistoryTexture, historyUV).rgb;
	history = SafeColour(history);

	// Variance clip (the box of mean +- stddev, limited to the min/max box)
	// (1.5: a tighter box clipped the history of jittered edge pixels, whose 3x3 holds both sides, so they never converged)
	const float gamma = 1.5;
	vec3 mean = m1 / 9.0;
	vec3 stddev = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
	vec3 clipMin = max(boxMin, mean - gamma * stddev);
	vec3 clipMax = min(boxMax, mean + gamma * stddev);
	vec3 historyYCoCg = RGBToYCoCg(history);
	vec3 clippedYCoCg = ClipToAABB(historyYCoCg, clipMin, clipMax);
	history = YCoCgToRGB(clippedYCoCg);

	// History weight: high while the history agrees with the neighbourhood (static & anti-aliased edges), lower as it's
	// rejected (clipped far) or as the pixel moves (each resample blurs a little). (Weighting by the current pixel's
	// luminance difference, as some TAAs do, kept jittered edges from converging: they always differ.)
	float rejection = length(historyYCoCg - clippedYCoCg) / max(clippedYCoCg.x, 0.05);
	float weight = mix(pushConstants.params.y, pushConstants.params.x, clamp(rejection * 2.0, 0.0, 1.0));
	float motionPixels = length(velocity * texSize);
	weight = clamp(weight - motionPixels * pushConstants.params.w, pushConstants.params.x, pushConstants.params.y);

	out_Colour = vec4(mix(current, history, weight), 1.0);
}
