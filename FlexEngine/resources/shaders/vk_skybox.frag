#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 ex_TexCoord;

layout (location = 0) out vec4 fragmentColor;

layout (binding = 0) uniform UBOConstant
{
	float exposure;
	// float time;
} uboConstant;

layout (binding = 1) uniform samplerCube cubemap;

float noise(float s)
{
	return fract(546354.846451 * s * s + 4564.4123);
}

void main()
{
	vec3 dir = normalize(ex_TexCoord);

	float n = noise(abs(ex_TexCoord.x + ex_TexCoord.y)) * 0.03; // Noise to prevent banding

	// vec3 top = vec3(0.15, 0.39, 0.65); // baby blue
	// vec3 mid = vec3(0.66, 0.86, 0.95); // ultra pale blue
	// vec3 btm = vec3(0.75, 0.91, 0.99); // off-white

	vec3 top = vec3(0.07, 0.09, 0.14); // dark pale blue
	vec3 mid = vec3(0.06, 0.08, 0.12); // pale blue
	vec3 btm = vec3(0.07, 0.07, 0.14); // dark-blue

	float h = sign(dir.y)*pow(abs(dir.y), 0.5) + n;

	float tw = max(h, 0.0);
	float mw = pow(1.0-abs(dir.y), 10.0) * 0.8;
	float bw = max(-h, 0.0);

	fragmentColor = vec4((top * tw) + (mid * mw) + (btm * bw), 1.0); return;

	fragmentColor = texture(cubemap, ex_TexCoord) * uboConstant.exposure;
}
