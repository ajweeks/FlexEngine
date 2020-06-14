#version 450

#define NUM_POINT_LIGHTS 8

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 ex_PositionWS;
layout (location = 1) in vec4 ex_Colour;
layout (location = 2) in vec3 ex_NormalWS;

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

struct OceanColours
{
	vec4 top;
	vec4 mid;
	vec4 btm;
	float fresnelFactor;
	float fresnelPower;
};

layout (binding = 0) uniform UBOConstant
{
	vec4 camPos;
	mat4 view;
	mat4 projection;
	DirectionalLight dirLight;
	OceanColours oceanColours;
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

	float upness = max(dot(N, normalize(vec3(0.0, 1.2, 0.4))), 0);

	float fresnel = clamp(pow(1.0-NoV+0.07, uboConstant.oceanColours.fresnelPower), 0, 1);

	float deepness = (0.5+fresnel*0.5) * pow(1.0-clamp(abs(V.y),0,1), 5.0);

	vec3 oceanTop = pow(uboConstant.oceanColours.top.xyz, vec3(2.2));
	vec3 oceanMid = pow(uboConstant.oceanColours.mid.xyz, vec3(2.2));
	vec3 oceanBtm = pow(uboConstant.oceanColours.btm.xyz, vec3(2.2));

	vec3 skyHorizon = vec3(0.66, 0.86, 0.95)*0.80;

	vec3 sky = SampleSkybox(R);

	vec3 waterCol = mix(oceanBtm, mix(oceanMid, oceanTop, clamp((deepness - 0.5) * 2.0, 0, 1)), clamp(deepness * 2.0, 0, 1));
	fragColor = vec4(mix(waterCol, sky, clamp(fresnel * uboConstant.oceanColours.fresnelFactor,0,1)), 1);

	vec4 posCS = uboConstant.view * vec4(ex_PositionWS, 1);	
	float depthFade = clamp(pow(pow(posCS.z, 1.0)*0.002, 1.65), 0, 1);
	fragColor = mix(fragColor, vec4(skyHorizon, 1.0), depthFade);

	//fragColor = vec4(SampleSkybox(R), 1);
	// fragColor = vec4(fresnel.xxx, 1);
	// fragColor = vec4(deepness.xxx, 1.0);
}
