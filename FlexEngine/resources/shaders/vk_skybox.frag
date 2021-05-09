#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 ex_TexCoord;

layout (location = 0) out vec4 fragmentColour;

struct SkyboxData
{
	vec4 colourTop;
	vec4 colourMid;
	vec4 colourBtm;
	vec4 colourFog;
};

layout (binding = 0) uniform UBOConstant
{
	SkyboxData skyboxData;
	vec4 time; // X: seconds elapsed since program start, Y: time of day [0,1]
} uboConstant;

layout (binding = 1) uniform sampler2D whiteNoise;
layout (binding = 2) uniform samplerCube cubemap;

float noise(float s)
{
	return fract(546354.846451 * s * s + 4564.4123);
}

void main()
{
	vec3 dir = normalize(ex_TexCoord);

	// Noise to prevent banding
	float n = noise(abs(ex_TexCoord.x + ex_TexCoord.y));

	vec2 uv = vec2(abs(dir.x), abs(dir.z)) * 2.0;
	if (dir.y < 0.25 && dir.y > -0.25)
	{
		uv = vec2(abs(dir.x), abs(dir.y)) * 2.0;
	}
	float n_w = texture(whiteNoise, uv).r;

	// Night theme
	//vec3 top = vec3(0.07, 0.09, 0.14); // dark pale blue
	//vec3 mid = vec3(0.06, 0.08, 0.12); // pale blue
	//vec3 btm = vec3(0.07, 0.07, 0.14); // dark-blue

	float h = sign(dir.y) * pow(abs(dir.y), 0.3) + n * 0.03;

	float tw = 1.0-max(h, 0.0);
	// float mw = pow(1.0-abs(dir.y), 10.0);
	float bw = max(-h, 0.0);

	float w0 = tw;
	float w1 = bw;
	float w2 = h > 0 ? 0.0 : 1.0;

	fragmentColour = vec4(clamp(
		mix(
			mix(uboConstant.skyboxData.colourTop.xyz, uboConstant.skyboxData.colourMid.xyz, w0),
			mix(uboConstant.skyboxData.colourMid.xyz, uboConstant.skyboxData.colourBtm.xyz, w1), w2),
			 0.0, 1.0), 1.0);

	float timeOfDay = uboConstant.time.y;
	float nightStart = 0.36;
	float nightEnd = 0.63;
	if (timeOfDay > nightStart && timeOfDay < nightEnd)
	{
		float nightProgress = (timeOfDay - nightStart) / (nightEnd - nightStart);
		float nightProgressParabola = 1.0-(nightProgress-0.5)*(nightProgress-0.5)*4.0;
		nightProgressParabola = pow(nightProgressParabola, 0.25);
		float starThreshold = mix(-100.0, -12.0, nightProgressParabola);
		fragmentColour += 0.9 * exp(starThreshold * n_w);
	}
}
