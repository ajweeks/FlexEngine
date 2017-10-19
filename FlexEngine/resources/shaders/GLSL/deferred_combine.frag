#version 400

// Deferred PBR combine

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct DirectionalLight 
{
	vec4 direction;
	vec4 color;
	bool enabled;
	float padding[3];
};
uniform DirectionalLight dirLight;

struct PointLight
{
	vec4 position;
	vec4 color;
	bool enabled;
	float padding[3];
};
#define NUMBER_POINT_LIGHTS 4
uniform PointLight pointLights[NUMBER_POINT_LIGHTS];

layout (binding = 0) uniform sampler2D positionMetallicFrameBufferSampler;
layout (binding = 1) uniform sampler2D normalRoughnessFrameBufferSampler;
layout (binding = 2) uniform sampler2D albedoAOFrameBufferSampler;

uniform vec4 camPos;

in vec2 ex_TexCoord;

out vec4 fragmentColor;

const float PI = 3.14159265359;

layout (binding = 3) uniform sampler2D brdfLUT;

uniform bool enableIrradianceSampler;
layout (binding = 4) uniform samplerCube irradianceSampler;

layout (binding = 5) uniform samplerCube prefilterMap;

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

vec3 DoLighting(vec3 radiance, vec3 N, vec3 V, vec3 L, 
	float roughness, float metallic, vec3 F0, vec3 albedo)
{
	vec3 H = normalize(V + L);

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

	return (kD * albedo / PI + specular) * radiance * NdotL;
}

void main()
{
    // Retrieve data from gbuffer
    vec3 worldPos = texture(positionMetallicFrameBufferSampler, ex_TexCoord).rgb;
    float metallic = texture(positionMetallicFrameBufferSampler, ex_TexCoord).a;

    vec3 N = texture(normalRoughnessFrameBufferSampler, ex_TexCoord).rgb;
    float roughness = texture(normalRoughnessFrameBufferSampler, ex_TexCoord).a;

    vec3 albedo = texture(albedoAOFrameBufferSampler, ex_TexCoord).rgb;
    float ao = texture(albedoAOFrameBufferSampler, ex_TexCoord).a;

	vec3 V = normalize(camPos.xyz - worldPos);
	vec3 R = reflect(-V, N);

	// If diaelectric, F0 should be 0.04, if metal it should be the albedo color
	vec3 F0 = vec3(0.04);
	F0 = mix(F0, albedo, metallic);

	// Reflectance equation
	vec3 Lo = vec3(0.0);
	for (int i = 0; i < NUMBER_POINT_LIGHTS; ++i)
	{
		if (!pointLights[i].enabled) continue;

		vec3 L = normalize(pointLights[i].position.xyz - worldPos);
		
		float distance = length(pointLights[i].position.xyz - worldPos);
		float attenuation = 1.0 / (distance * distance);
		vec3 radiance = pointLights[i].color.rgb * attenuation;
	
		Lo += DoLighting(radiance, N, V, L, roughness, metallic, F0, albedo);
	}

	if (dirLight.enabled)
	{
		vec3 L = normalize(dirLight.direction.xyz);
		vec3 radiance = dirLight.color.rgb;
		
		Lo += DoLighting(radiance, N, V, L, roughness, metallic, F0, albedo);
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

	fragmentColor = vec4(color, 1.0);
}
