#version 450

layout (push_constant) uniform PushConstants
{
	layout (offset = 0) mat4 view;
	layout (offset = 64) mat4 proj;
	layout (offset = 128) uint textureLayer;
} pushConstants;

layout (binding = 0) uniform UBODynamic
{
	mat4 model;
	vec4 colourMultiplier;
	bool enableAlbedoSampler;
} uboDynamic;

layout (binding = 1) uniform sampler2DArray in_Texture;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Colour;

// Only used by the shadow map debug view (VulkanRenderer::EnqueueShadowDebugSprites): shows a layer of a shadow map array
void main()
{
	if (!uboDynamic.enableAlbedoSampler)
	{
		out_Colour = uboDynamic.colourMultiplier;
		return;
	}

	vec2 uv = vec2(ex_TexCoord.x, 1 - ex_TexCoord.y);
	// Reversed-Z: 0 is the far plane/cleared (nothing there), larger is nearer the light
	float depth = texture(in_Texture, vec3(uv, pushConstants.textureLayer)).r;
	vec3 col;
	if (depth <= 0.0)
	{
		col = vec3(0.04, 0.05, 0.12);
	}
	else
	{
		// Brightened, since perspective (point/spot) depth is small for all but the nearest casters
		float v = pow(clamp(depth, 0.0, 1.0), 0.3);
		col = vec3(0.15 + 0.85 * v);
	}

	// A border in the tile's tint
	vec2 edge = min(ex_TexCoord, 1.0 - ex_TexCoord);
	if (min(edge.x, edge.y) < 0.025)
	{
		col = vec3(1.0);
	}

	out_Colour = vec4(col * uboDynamic.colourMultiplier.rgb, 1.0);
}
