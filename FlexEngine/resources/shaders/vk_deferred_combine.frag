#version 420

// Deferred PBR Combine

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 fragColor;

const float PI = 3.14159265359;

struct DirectionalLight 
{
	vec3 direction;
	int enabled;
	vec3 color;
	float brightness;
};

struct PointLight 
{
	vec3 position;
	int enabled;
	vec3 color;
	float brightness;
};
#define NUMBER_POINT_LIGHTS 8

layout (binding = 0) uniform UBOConstant
{
	vec4 camPos;
	mat4 invView;
	mat4 invProj;
	DirectionalLight dirLight;
	PointLight pointLights[NUMBER_POINT_LIGHTS];
	int enableSSAO;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	bool enableIrradianceSampler;
} uboDynamic;

layout (binding = 2) uniform sampler2D brdfLUT;
layout (binding = 3) uniform samplerCube irradianceSampler;
layout (binding = 4) uniform samplerCube prefilterMap;

layout (binding = 5) uniform sampler2D depthBuffer;
layout (binding = 6) uniform sampler2D ssaoBuffer;
layout (binding = 7) uniform sampler2D normalRoughnessTex;
layout (binding = 8) uniform sampler2D albedoMetallicTex;

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

vec3 DoLighting(vec3 radiance, vec3 N, vec3 V, vec3 L, float NoV, float NoL,
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

	return (kD * albedo / PI + specular) * radiance * NoL;
}

vec3 reconstructWSPosFromDepth(vec2 uv, float depth)
{
	float x = uv.x * 2.0f - 1.0f;
	float y = (1.0f - uv.y) * 2.0f - 1.0f;
	vec4 pos = vec4(x, y, depth, 1.0f);
	vec4 posVS = uboConstant.invProj * pos;
	vec3 posNDC = posVS.xyz / posVS.w;
	return (uboConstant.invView * vec4(posNDC, 1)).xyz;
}

void main()
{
    vec3 N = texture(normalRoughnessTex, ex_TexCoord).rgb;
    N = mat3(uboConstant.invView) * N; // view space -> world space
    float roughness = texture(normalRoughnessTex, ex_TexCoord).a;

    float depth = texture(depthBuffer, ex_TexCoord).r;

    vec3 worldPos = reconstructWSPosFromDepth(ex_TexCoord, depth);

    vec3 albedo = texture(albedoMetallicTex, ex_TexCoord).rgb;
    float metallic = texture(albedoMetallicTex, ex_TexCoord).a;

    float ssao = uboConstant.enableSSAO != 0 ? texture(ssaoBuffer, ex_TexCoord).r : 1.0f;

    // fragColor = vec4(ssao, ssao, ssao, 1); return;

	vec3 V = normalize(uboConstant.camPos.xyz - worldPos);
	vec3 R = reflect(-V, N);

	float NoV = max(dot(N, V), 0.0);

	// If diaelectric, F0 should be 0.04, if metal it should be the albedo color
	vec3 F0 = vec3(0.04);
	F0 = mix(F0, albedo, metallic);

	// Reflectance equation
	vec3 Lo = vec3(0.0);
	for (int i = 0; i < NUMBER_POINT_LIGHTS; ++i)
	{
		if (uboConstant.pointLights[i].enabled == 0)
		{
			continue;
		}

		float distance = length(uboConstant.pointLights[i].position - worldPos);

		if (distance > 125)
		{
			// TODO: Define radius on point lights individually
			continue;
		}

		// Pretend point lights have a radius of 1cm to avoid division by 0
		float attenuation = 1.0 / max((distance * distance), 0.001);
		vec3 L = normalize(uboConstant.pointLights[i].position - worldPos);
		vec3 radiance = uboConstant.pointLights[i].color.rgb * attenuation * uboConstant.pointLights[i].brightness;
		float NoL = max(dot(N, L), 0.0);

		Lo += DoLighting(radiance, N, V, L, NoV, NoL, roughness, metallic, F0, albedo);
	}

	if (uboConstant.dirLight.enabled != 0)
	{
		vec3 L = normalize(uboConstant.dirLight.direction);
		vec3 radiance = uboConstant.dirLight.color.rgb * uboConstant.dirLight.brightness;
		float NoL = max(dot(N, L), 0.0);

		Lo += DoLighting(radiance, N, V, L, NoV, NoL, 1, 1, F0, vec3(1.0));
	}

	vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

	vec3 ambient;
	if (uboDynamic.enableIrradianceSampler)
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

	    ambient = (kD * diffuse + specular);
	}
	else
	{
		ambient = vec3(0.03) * albedo;
	}

	vec3 color = ambient + Lo * pow(ssao,2.0f); // TODO: Apply SSAO to ambient term

	color = color / (color + vec3(1.0f)); // Reinhard tone-mapping
	color = pow(color, vec3(1.0f / 2.2f)); // Gamma correction

	fragColor = vec4(color, 1.0);

	// fragColor = vec4(F, 1);

	// Visualize world pos:
	// fragColor = vec4(worldPos*0.1, 1); return;

	// Visualize normals:
	// fragColor = vec4(N*0.5+0.5, 1); return;

	// Visualize screen coords:
	//fragColor = vec4(ex_TexCoord, 0, 1); return;

	// Visualize metallic:
	//fragColor = vec4(metallic, metallic, metallic, 1); return;
}
