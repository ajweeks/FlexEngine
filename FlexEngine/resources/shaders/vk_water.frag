#version 450

#define NUM_POINT_LIGHTS 8

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 ex_NormalWS;
layout (location = 1) in vec3 ex_PositionWS;

layout (location = 0) out vec4 fragColor;

// layout (binding = 1) uniform UBODynamic
// {
	// int enableIrradianceSampler;
// } uboDynamic;

// TODO: Move to common header
struct DirectionalLight 
{
	vec3 direction;
	int enabled;
	vec3 color;
	float brightness;
	int castShadows;
	float shadowDarkness;
	vec2 _dirLightPad;
};

layout (binding = 0) uniform UBOConstant
{
	vec4 camPos;
	mat4 viewProjection;
	DirectionalLight dirLight;
} uboConstant;

vec3 SampleSkybox(vec3 dir)
{
	vec3 top = vec3(0.22, 0.58, 0.88);
	vec3 mid = vec3(0.66, 0.86, 0.95);
	vec3 btm = vec3(0.75, 0.91, 0.99);

	float h = sign(dir.y)*pow(abs(dir.y), 0.5);

	float tw = max(h, 0.0);
	float mw = pow(1.0-abs(dir.y), 10.0) * 0.8;
	float bw = max(-h, 0.0);

	return vec3(clamp((top * tw) + (mid * mw) + (btm * bw), 0, 1));
}

void main()
{
	vec3 N = normalize(ex_NormalWS);

	vec3 light = vec3(0);
	if (uboConstant.dirLight.enabled != 0)
	{
		light = max(dot(ex_NormalWS, uboConstant.dirLight.direction), 0.0) * uboConstant.dirLight.brightness * uboConstant.dirLight.color;
	}

	vec3 V = normalize(uboConstant.camPos.xyz - ex_PositionWS);
	vec3 R = reflect(-V, N);

	float NoV = max(dot(N, V), 0.0);

	float fresnel = clamp(pow(1.0-NoV+0.07, 7.0), 0, 1);

	float deepness = pow(1.0-clamp(abs(V.y),0,1), 3.0);

	vec3 black = vec3(0.005, 0.01, 0.03);
	vec3 deepBlue = vec3(0.02, 0.06, 0.25);
	vec3 lightBlue = vec3(0.1, 0.15, 0.3);

	vec3 sky = SampleSkybox(R);

	fragColor = vec4(mix(mix(black, deepBlue, deepness), sky, clamp(fresnel,0,1)), 1);
	//fragColor = vec4(SampleSkybox(R), 1);
	//fragColor = vec4(clamp(R,0,1), 1);
}
