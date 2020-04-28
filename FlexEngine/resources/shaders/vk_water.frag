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

layout (binding = 2) uniform samplerCube skySampler;

void main()
{
	vec3 N = normalize(ex_NormalWS);

	vec3 light = vec3(0);
	if (uboConstant.dirLight.enabled != 0)
	{
		light = max(dot(ex_NormalWS, uboConstant.dirLight.direction), 0.0) * uboConstant.dirLight.brightness * uboConstant.dirLight.color;
	}

	vec3 sky = texture(skySampler, N).xyz;

	vec3 V = normalize(uboConstant.camPos.xyz - ex_PositionWS);
	vec3 R = reflect(-V, N);

	float NoV = max(dot(N, V), 0.0);

	float fresnel = max(pow(1.0-NoV, 7.0), 0.0);

	float deepness = pow(1.0-clamp(abs(V.y),0,1), 3.0);

	vec3 black = vec3(0.005, 0.01, 0.03);
	vec3 deepBlue = vec3(0.02, 0.06, 0.25);
	vec3 lightBlue = vec3(0.1, 0.15, 0.3);

	fragColor = vec4(mix(mix(black, deepBlue, deepness), lightBlue, clamp(fresnel,0,1)) * sky, 1); return;
}
