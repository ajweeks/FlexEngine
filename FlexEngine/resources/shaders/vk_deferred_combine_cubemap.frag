#version 450

// Deferred PBR Combine

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 ex_WorldPosition;

layout (location = 0) out vec4 fragColour;

const float PI = 3.14159265359;

struct DirectionalLight 
{
	vec4 direction;
	vec4 colour;
	bool enabled;
};

struct PointLight 
{
	vec4 position;
	vec4 colour;
	bool enabled;
};
#define NUMBER_POINT_LIGHTS 4

layout (binding = 0) uniform UBOConstant
{
	mat4 viewProjection;
	vec4 camPos;
	DirectionalLight dirLight;
	PointLight pointLights[NUMBER_POINT_LIGHTS];
} uboConstant;

layout (binding = 2) uniform sampler2D brdfLUT;
layout (binding = 3) uniform samplerCube irradianceSampler;
layout (binding = 4) uniform samplerCube prefilterMap;

// Very out of date!
layout (binding = 5) uniform samplerCube positionMetallicFrameBufferSampler;
layout (binding = 6) uniform samplerCube normalRoughnessFrameBufferSampler;
layout (binding = 7) uniform samplerCube albedoAOFrameBufferSampler;

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
	// Retrieve data from gbuffer
    vec3 worldPos = texture(positionMetallicFrameBufferSampler, ex_WorldPosition).rgb;
    float metallic = texture(positionMetallicFrameBufferSampler, ex_WorldPosition).a;

    vec3 N = texture(normalRoughnessFrameBufferSampler, ex_WorldPosition).rgb;
    float roughness = texture(normalRoughnessFrameBufferSampler, ex_WorldPosition).a;

    vec3 albedo = texture(albedoAOFrameBufferSampler, ex_WorldPosition).rgb;
    float ao = texture(albedoAOFrameBufferSampler, ex_WorldPosition).a;

	vec3 V = normalize(uboConstant.camPos.xyz - worldPos);
	vec3 R = reflect(-V, N);

	// If diaelectric, F0 should be 0.04, if metal it should be the albedo colour
	vec3 F0 = vec3(0.04);
	F0 = mix(F0, albedo, metallic);

	// Reflectance equation
	vec3 Lo = vec3(0.0);
	for (int i = 0; i < NUMBER_POINT_LIGHTS; ++i)
	{
		if (!uboConstant.pointLights[i].enabled) continue;
		vec3 L = normalize(uboConstant.pointLights[i].position.xyz - worldPos);
		vec3 H = normalize(V + L);

		float distance = length(uboConstant.pointLights[i].position.xyz - worldPos);
		float attenuation = 1.0 / (distance * distance);
		vec3 radiance = uboConstant.pointLights[i].colour.rgb * attenuation;
		
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

	// Diffse ambient term (IBL)
	vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;	  
    vec3 irradiance = texture(irradianceSampler, N).rgb;
    vec3 diffuse = irradiance * albedo;

	// Specular ambient term (IBL)
	const float MAX_REFLECTION_LOAD = 5.0;
	vec3 prefilteredColour = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOAD).rgb;
	vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
	vec3 specular = prefilteredColour * (F * brdf.x + brdf.y);

	vec3 ambient = (kD * diffuse + specular) * ao;


	vec3 colour = ambient + Lo;

	colour = colour / (colour + vec3(1.0f)); // Reinhard tone-mapping
	colour = pow(colour, vec3(1.0f / 2.2f)); // Gamma correction

	fragColour = vec4(colour, 1.0);

	// Visualize normal map:
	//fragColour = vec4(texture(normalSampler, ex_TexCoord).xyz, 1); return;

	// Visualize normals:
	//fragColour = vec4(N, 1); return;

	// Visualize tangents:
	//fragColour = vec4(vec3(ex_TBN[0]), 1); return;

	// Visualize texCoords:
	//fragColour = vec4(ex_TexCoord, 0, 1); return;

	// Visualize metallic:
	//fragColour = vec4(metallic, metallic, metallic, 1); return;
}
