#version 400

// Deferred PBR combine

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

in vec2 ex_TexCoord;

out vec4 fragmentColor;

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
	float padding[2];
};
#define NUMBER_POINT_LIGHTS 4
uniform PointLight pointLights[NUMBER_POINT_LIGHTS];

uniform vec4 camPos;
uniform bool enableIrradianceSampler;
uniform float exposure = 1.0;
uniform mat4 lightViewProj;
uniform bool castShadows = true;
uniform float shadowDarkness = 0.0;
const float PI = 3.14159265359;

layout (binding = 0) uniform sampler2D positionMetallicFrameBufferSampler;
layout (binding = 1) uniform sampler2D normalRoughnessFrameBufferSampler;
layout (binding = 2) uniform sampler2D albedoAOFrameBufferSampler;
layout (binding = 3) uniform sampler2D brdfLUT;
layout (binding = 4) uniform sampler2D shadowMap;
layout (binding = 5) uniform samplerCube irradianceSampler;
layout (binding = 6) uniform samplerCube prefilterMap;

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
	float a2 = roughness * roughness;
	float NoH = max(dot(N, H), 0.0);
	float f = (NoH * a2 - NoH) * NoH + 1.0;
	return a2 / (PI * f * f);
}

float GeometrySchlickGGX(float NoV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float nom = NoV;
	float denom = NoV * (1.0 - k) + k;

	return nom / denom;
}

float GeometrySmith(float NoV, float NoL, float roughness)
{
	float ggx2 = GeometrySchlickGGX(NoV, roughness);
	float ggx1 = GeometrySchlickGGX(NoL, roughness);

	return ggx1 * ggx2;
}

vec3 DoLighting(vec3 radiance, vec3 N, vec3 V, vec3 L, float NoV, float NoL,
	float roughness, float metallic, vec3 F0, vec3 albedo)
{
	vec3 H = normalize(V + L);

	// Cook-Torrance BRDF
	float NDF = DistributionGGX(N, H, roughness);
	float G = GeometrySmith(NoV, NoL, roughness);
	vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

	vec3 kS = F;
	vec3 kD = vec3(1.0) - kS;
	kD *= 1.0 - metallic; // Pure metals have no diffuse lighting

	vec3 nominator = NDF * G * F;
	float denominator = 4 * NoV * NoL + 0.001; // Add epsilon to prevent divide by zero
	vec3 specular = nominator / denominator;

	return (kD * albedo / PI + specular) * radiance * NoL;
}

// "UDN normal map blending": [Hill12]
// vec3 t = texture(baseMap,   uv).xyz * 2.0 - 1.0;
// vec3 u = texture(detailMap, uv).xyz * 2.0 - 1.0;
// vec3 r = normalize(t.xy + u.xy, t.z);
// return r;
//

void main()
{
    // Retrieve data from gbuffer
    vec3 worldPos = texture(positionMetallicFrameBufferSampler, ex_TexCoord).rgb;
    float metallic = texture(positionMetallicFrameBufferSampler, ex_TexCoord).a;

    vec3 N = texture(normalRoughnessFrameBufferSampler, ex_TexCoord).rgb;
    float roughness = texture(normalRoughnessFrameBufferSampler, ex_TexCoord).a;
    // If using half floats: (prevent division by zero)
    //roughness = max(roughness, 0.089);
    // If using fp32:
    roughness = max(roughness, 0.045);

    vec3 albedo = texture(albedoAOFrameBufferSampler, ex_TexCoord).rgb;
    float ao = texture(albedoAOFrameBufferSampler, ex_TexCoord).a;

	vec3 V = normalize(camPos.xyz - worldPos);
	vec3 R = reflect(-V, N);

	float NoV = max(dot(N, V), 0.0);

	// If diaelectric, F0 should be 0.04, if metal it should be the albedo color
	vec3 F0 = vec3(0.04);
	F0 = mix(F0, albedo, metallic);

	// Reflectance equation
	vec3 Lo = vec3(0.0);
	for (int i = 0; i < NUMBER_POINT_LIGHTS; ++i)
	{
		if (!pointLights[i].enabled)
		{
			continue;
		}

		float distance = length(pointLights[i].position.xyz - worldPos);

		// This value still causes a visible harsh edge but our maps won't likely be 
		// this large and we'll have overlapping lights which will hide this
		if (distance > 125)
		{
			continue;
		}

		// Pretend point lights have a radius of 1cm to avoid division by 0
		float attenuation = 1.0 / max((distance * distance), 0.001);
		vec3 L = normalize(pointLights[i].position.xyz - worldPos);
		vec3 radiance = pointLights[i].color.rgb * attenuation;
		float NoL = max(dot(N, L), 0.0);
	
		Lo += DoLighting(radiance, N, V, L, NoV, NoL, roughness, metallic, F0, albedo);
	}

	if (dirLight.enabled)
	{
		vec3 L = normalize(dirLight.direction.xyz);
		vec3 radiance = dirLight.color.rgb;
		float NoL = max(dot(N, L), 0.0);
		
		float dirLightShadow = 1.0;
		if (castShadows)
		{	
			vec3 transformedShadowPos = vec3(lightViewProj * vec4(worldPos, 1.0));

			if (transformedShadowPos.z > 1.0)
			{
				// Outside of light frustum
				dirLightShadow = 1.0;
			}
			else
			{
				float baseBias = 0.005;
				float bias = max(baseBias * (1.0 - NoL), baseBias * 0.01);

				float shadowDepth = texture(shadowMap, transformedShadowPos.xy).r;
				//float shadowDepth = texture(shadowMap, transformedShadowPos.xyz, bias);

				if (shadowDepth < transformedShadowPos.z - bias)
				{
					dirLightShadow = shadowDarkness;
				}
			}
		}

		Lo += DoLighting(radiance, N, V, L, NoV, NoL, roughness, metallic, F0, albedo) * dirLightShadow;
	}

	vec3 F = FresnelSchlickRoughness(NoV, F0, roughness);

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
		vec2 brdf = texture(brdfLUT, vec2(NoV, roughness)).rg;
		vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

		// Prevent specular light leaking [Russell15]
		float horizon = min(1.0 + dot(R, N), 1.0);
		specular *= horizon * horizon;

	    ambient = (kD * diffuse + specular) * ao;
	}
	else
	{
		ambient = vec3(0.03) * albedo * ao;
	}

	vec3 color = ambient + Lo;

	color *= exposure;

	fragmentColor = vec4(color, 1.0);
}
