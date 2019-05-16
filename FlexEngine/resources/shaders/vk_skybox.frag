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

	// float n = noise(abs(ex_TexCoord.x + ex_TexCoord.y)) * 0.1; // Noise to prevent banding
	// vec4 col = mix(vec4(0.04, 0.03, 0.03, 1), vec4(0.01, 0.025, 0.05, 1), max(0, dir.y + 0.5 + n));
	// float t = mod(uboConstant.time, 2.0) - 1.0;
	// float tt = sin(uboConstant.time*2.0)+1.0;
	// float d = mix(dir.x+dir.z/2.0, dir.y, tt);
	// col = mix(col, vec4(1, 0, 0, 1), min(smoothstep(0.0f, 0.02f, d - tt), smoothstep(-0.02f, 0.0f, tt - d)));

	// return;
	

	fragmentColor = texture(cubemap, ex_TexCoord) * uboConstant.exposure;
}
