#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct PointLight 
{
	vec4 position;

	vec4 color;

	bool enabled;
	float padding[3];
};
#define NUMBER_POINT_LIGHTS 4
uniform PointLight pointLights[NUMBER_POINT_LIGHTS];

// Material variables
uniform vec4 constAlbedo;
uniform bool enableAlbedoSampler;
layout (binding = 0) uniform sampler2D albedoSampler;

uniform float constMetallic;
uniform bool enableMetallicSampler;
layout (binding = 1) uniform sampler2D metallicSampler;

uniform float constRoughness;
uniform bool enableRoughnessSampler;
layout (binding = 2) uniform sampler2D roughnessSampler;

uniform float constAO;
uniform bool enableAOSampler;
layout (binding = 3) uniform sampler2D aoSampler;

uniform bool enableNormalSampler;
layout (binding = 4) uniform sampler2D normalSampler;

layout (binding = 5) uniform sampler2D brdfLUT;

uniform bool enableIrradianceSampler;
layout (binding = 6) uniform samplerCube irradianceSampler;

layout (binding = 7) uniform samplerCube prefilterMap;

uniform vec4 camPos;

in vec3 ex_WorldPos;
in vec2 ex_TexCoord;
in mat3 ex_TBN;

out vec4 fragColor;

const float PI = 3.14159265359;

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float NdotH2 = NdotH * NdotH;

	float nom = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float nom = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

void main() 
{
	vec3 albedo = enableAlbedoSampler ? texture(albedoSampler, ex_TexCoord).rgb : vec3(constAlbedo);
	float metallic = enableMetallicSampler ? texture(metallicSampler, ex_TexCoord).r : constMetallic;
	float roughness = enableRoughnessSampler ? texture(roughnessSampler, ex_TexCoord).r : constRoughness;
	float ao = enableAOSampler ? texture(aoSampler, ex_TexCoord).r : constAO;

	vec3 Normal = enableNormalSampler ? (ex_TBN * (texture(normalSampler, ex_TexCoord).xyz * 2 - 1)) : ex_TBN[2];

	vec3 N = normalize(Normal);
	vec3 V = normalize(camPos.xyz - ex_WorldPos);
	vec3 R = reflect(-V, N);

	// Visualize normals:
	//fragColor = vec4(N, 1); return;

	// Visualize texCoords:
	//fragColor = vec4(ex_TexCoord, 0, 1); return;

	// If diaelectric, F0 should be 0.04, if metal it should be the albedo color
	vec3 F0 = vec3(0.04);
	F0 = mix(F0, albedo, metallic);

	// Reflectance equation
	vec3 Lo = vec3(0.0);
	for (int i = 0; i < NUMBER_POINT_LIGHTS; ++i)
	{
		if (!pointLights[i].enabled) continue;
		vec3 L = normalize(pointLights[i].position.xyz - ex_WorldPos);
		vec3 H = normalize(V + L);

		float distance = length(pointLights[i].position.xyz - ex_WorldPos);
		float attenuation = 1.0 / (distance * distance);
		vec3 radiance = pointLights[i].color.rgb * attenuation;
		
		// Cook-Torrance BRDF
		float NDF = DistributionGGX(N, H, roughness);
		float G = GeometrySmith(N, V, L, roughness);
		vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

		vec3 kS = F;
		vec3 kD = vec3(1.0) - kS;
		kD *= 1.0 - metallic; // Pure metals have no diffuse lighting

		vec3 nominator = NDF * G * F;
		float denominator = 4 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001; // Add epsilon to prevent divide by zero
		vec3 specular = nominator / denominator;

		float NdotL = max(dot(N, L), 0.0);
		Lo += (kD * albedo / PI + specular) * radiance * NdotL;
	}

	vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

	vec3 ambient;
	if (enableIrradianceSampler)
	{
		// Diffse ambient term (IBL)
		vec3 kS = F;
	    vec3 kD = 1.0 - kS;
	    kD *= 1.0 - metallic;	  
	    vec3 irradiance = texture(irradianceSampler, N).rgb;
	    vec3 diffuse = irradiance * albedo;

		// Specular ambient term (IBL)
		const float MAX_REFLECTION_LOAD = 5.0;
		vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOAD).rgb;
		vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
		vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

	    ambient = (kD * diffuse + specular) * ao;
	}
	else
	{
		ambient = vec3(0.03) * albedo * ao;
	}

	vec3 color = ambient + Lo;

	// TODO: Once we add post processing that requires HDR don't do this calculation here:
	color = color / (color + vec3(1.0)); // Reinhard
	color = pow(color, vec3(1.0 / 2.2)); // Gamma correct

	fragColor = vec4(color, 1.0);
}
