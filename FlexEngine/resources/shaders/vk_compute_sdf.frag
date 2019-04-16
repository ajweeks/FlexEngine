#version 450

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform UBODynamic
{
	vec4 charResXYspreadZsampleDensityW;
	int texChannel;
} uboDynamic;

layout (binding = 1) uniform sampler2D highResTex;

void main()
{
	vec2 charRes = uboDynamic.charResXYspreadZsampleDensityW.xy;
	float spread = uboDynamic.charResXYspreadZsampleDensityW.z;
	float sampleDensity = uboDynamic.charResXYspreadZsampleDensityW.w;

	bool insideChar = texture(highResTex, ex_TexCoord).r > 0.5;

	// Get closest opposite
	vec2 startPos = ex_TexCoord - (vec2(spread) / charRes);
	vec2 delta = vec2(1.0 / (spread * sampleDensity * 2.0));
	float closest = spread;
	for (int y = 0; y < int(spread * sampleDensity * 2.0); ++y)
	{
		for (int x = 0; x < int(spread * sampleDensity * 2.0); ++x)
		{
			vec2 samplePos = startPos + vec2(x, y) * delta;
			vec2 diff = (ex_TexCoord - samplePos) * charRes;
			diff.x *= charRes.x / charRes.y;
			float dist = length(diff);
			if (dist < closest)
			{
				bool sampleInsideChar = texture(highResTex, samplePos).r > 0.5;
				if (sampleInsideChar != insideChar)
				{
					closest = dist;
				}
			}
		}
	}

	float diff = closest / (spread * 2.0);
	float val = 0.5 + (insideChar ? diff : -diff);

	vec4 color = vec4(0.0);
	color[uboDynamic.texChannel] = val;
	outColor = color;
}