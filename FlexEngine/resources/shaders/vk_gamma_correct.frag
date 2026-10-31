#version 450

// Final copy of the graded scene. Shaders already write display (sRGB encoded) values (see vk_misc.glsl), so this
// must not encode them again (doing so, after a second tone map in post processing, washed out the whole image).
// With FXAA on (anti-aliasing mode 1, see Renderer::SetAntiAliasing) it also smooths edges: FXAA 3.11 (quality
// preset style), with fewer edge search steps at lower shader quality levels.

layout (constant_id = 2) const int QUALITY_LEVEL = 1;

layout (binding = 0) uniform sampler2D in_Texture;

layout (push_constant) uniform PushConstants
{
	// x: 1 = FXAA on, y: subpixel aliasing removal [0, 1]
	vec4 params;
} pushConstants;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Colour;

const float EDGE_THRESHOLD_MIN = 0.0312;
const float EDGE_THRESHOLD_MAX = 0.125;

float Luma(vec3 rgb)
{
	return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

float LumaAt(vec2 uv)
{
	return Luma(textureLod(in_Texture, uv, 0.0).rgb);
}

// (texel offsets must be compile time constants)
#define LumaOffset(uv, offset) Luma(textureLodOffset(in_Texture, uv, 0.0, offset).rgb)

vec3 FXAA(vec2 uv, vec2 texel)
{
	vec3 colourCentre = textureLod(in_Texture, uv, 0.0).rgb;
	float lumaCentre = Luma(colourCentre);
	float lumaDown = LumaOffset(uv, ivec2(0, 1));
	float lumaUp = LumaOffset(uv, ivec2(0, -1));
	float lumaLeft = LumaOffset(uv, ivec2(-1, 0));
	float lumaRight = LumaOffset(uv, ivec2(1, 0));

	float lumaMin = min(lumaCentre, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
	float lumaMax = max(lumaCentre, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
	float lumaRange = lumaMax - lumaMin;
	if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX))
	{
		return colourCentre;
	}

	float lumaDownLeft = LumaOffset(uv, ivec2(-1, 1));
	float lumaUpRight = LumaOffset(uv, ivec2(1, -1));
	float lumaUpLeft = LumaOffset(uv, ivec2(-1, -1));
	float lumaDownRight = LumaOffset(uv, ivec2(1, 1));

	float lumaDownUp = lumaDown + lumaUp;
	float lumaLeftRight = lumaLeft + lumaRight;
	float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
	float lumaDownCorners = lumaDownLeft + lumaDownRight;
	float lumaRightCorners = lumaDownRight + lumaUpRight;
	float lumaUpCorners = lumaUpRight + lumaUpLeft;

	float edgeHorizontal = abs(-2.0 * lumaLeft + lumaLeftCorners) + abs(-2.0 * lumaCentre + lumaDownUp) * 2.0 + abs(-2.0 * lumaRight + lumaRightCorners);
	float edgeVertical = abs(-2.0 * lumaUp + lumaUpCorners) + abs(-2.0 * lumaCentre + lumaLeftRight) * 2.0 + abs(-2.0 * lumaDown + lumaDownCorners);
	bool bHorizontal = edgeHorizontal >= edgeVertical;

	float luma1 = bHorizontal ? lumaUp : lumaLeft;
	float luma2 = bHorizontal ? lumaDown : lumaRight;
	float gradient1 = luma1 - lumaCentre;
	float gradient2 = luma2 - lumaCentre;
	bool bSteepest1 = abs(gradient1) >= abs(gradient2);
	float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

	// (uv's +y is down the screen: "up" is -y)
	float stepLength = bHorizontal ? texel.y : texel.x;
	float lumaLocalAverage;
	if (bSteepest1)
	{
		stepLength = -stepLength;
		lumaLocalAverage = 0.5 * (luma1 + lumaCentre);
	}
	else
	{
		lumaLocalAverage = 0.5 * (luma2 + lumaCentre);
	}

	vec2 currentUV = uv;
	if (bHorizontal)
	{
		currentUV.y += stepLength * 0.5;
	}
	else
	{
		currentUV.x += stepLength * 0.5;
	}

	// Search along the edge in both directions for its ends
	vec2 offset = bHorizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
	vec2 uv1 = currentUV - offset;
	vec2 uv2 = currentUV + offset;
	float lumaEnd1 = LumaAt(uv1) - lumaLocalAverage;
	float lumaEnd2 = LumaAt(uv2) - lumaLocalAverage;
	bool bReached1 = abs(lumaEnd1) >= gradientScaled;
	bool bReached2 = abs(lumaEnd2) >= gradientScaled;

	const float QUALITY_STEPS[12] = float[12](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);
	int stepCount = QUALITY_LEVEL <= 0 ? 6 : (QUALITY_LEVEL == 1 ? 9 : 12);
	if (!bReached1)
	{
		uv1 -= offset;
	}
	if (!bReached2)
	{
		uv2 += offset;
	}
	for (int i = 2; i < stepCount && !(bReached1 && bReached2); ++i)
	{
		if (!bReached1)
		{
			lumaEnd1 = LumaAt(uv1) - lumaLocalAverage;
			bReached1 = abs(lumaEnd1) >= gradientScaled;
			if (!bReached1)
			{
				uv1 -= offset * QUALITY_STEPS[i];
			}
		}
		if (!bReached2)
		{
			lumaEnd2 = LumaAt(uv2) - lumaLocalAverage;
			bReached2 = abs(lumaEnd2) >= gradientScaled;
			if (!bReached2)
			{
				uv2 += offset * QUALITY_STEPS[i];
			}
		}
	}

	float distance1 = bHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
	float distance2 = bHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
	bool bDirection1 = distance1 < distance2;
	float distanceFinal = min(distance1, distance2);
	float edgeThickness = distance1 + distance2;

	bool bCentreSmaller = lumaCentre < lumaLocalAverage;
	bool bCorrectVariation = ((bDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != bCentreSmaller;
	float pixelOffset = bCorrectVariation ? (-distanceFinal / edgeThickness + 0.5) : 0.0;

	// Sub pixel aliasing (single pixel features)
	float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);
	float subPixelOffset1 = clamp(abs(lumaAverage - lumaCentre) / lumaRange, 0.0, 1.0);
	float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;
	float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * pushConstants.params.y;
	pixelOffset = max(pixelOffset, subPixelOffsetFinal);

	vec2 finalUV = uv;
	if (bHorizontal)
	{
		finalUV.y += pixelOffset * stepLength;
	}
	else
	{
		finalUV.x += pixelOffset * stepLength;
	}
	return textureLod(in_Texture, finalUV, 0.0).rgb;
}

void main()
{
	if (pushConstants.params.x > 0.5)
	{
		vec2 texel = 1.0 / vec2(textureSize(in_Texture, 0));
		out_Colour = vec4(FXAA(ex_TexCoord, texel), 1.0);
	}
	else
	{
		out_Colour = texture(in_Texture, ex_TexCoord);
	}
}
