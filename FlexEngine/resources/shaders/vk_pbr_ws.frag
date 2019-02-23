#version 450

// Deferred PBR

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 viewProjection;
	float blendSharpness;
	float textureScale;
} uboConstant;

// Updated once per object
layout (binding = 1) uniform UBODynamic
{
	mat4 model;

	// Constant values to use when not using samplers
	vec4 constAlbedo;
	float constMetallic;
	float constRoughness;
	float constAO;

	// PBR samplers
	bool enableAlbedoSampler;
	bool enableMetallicSampler;
	bool enableRoughnessSampler;
	bool enableAOSampler;
	bool enableNormalSampler;
} uboDynamic;

layout (location = 0) in vec3 ex_WorldPos;
layout (location = 1) in vec2 ex_TexCoord;
layout (location = 2) in mat3 ex_TBN;

layout (binding = 2) uniform sampler2D albedoSampler;
layout (binding = 3) uniform sampler2D metallicSampler;
layout (binding = 4) uniform sampler2D roughnessSampler;
layout (binding = 5) uniform sampler2D aoSampler;
layout (binding = 6) uniform sampler2D normalSampler;

layout (location = 0) out vec4 outPositionMetallic;
layout (location = 1) out vec4 outNormalRoughness;
layout (location = 2) out vec4 outAlbedoAO;

void main() 
{
    vec2 uvX = ex_WorldPos.zy * uboConstant.textureScale;
    vec2 uvY = ex_WorldPos.xz * uboConstant.textureScale;
    vec2 uvZ = ex_WorldPos.xy * uboConstant.textureScale;

    vec3 geomNorm = ex_TBN[2];

	vec3 albedo;
    if (uboDynamic.enableAlbedoSampler)
    {
		vec3 albedoX = texture(albedoSampler, uvX).rgb * uboDynamic.constAlbedo.rgb;
		vec3 albedoY = texture(albedoSampler, uvY).rgb * uboDynamic.constAlbedo.rgb;
		vec3 albedoZ = texture(albedoSampler, uvZ).rgb * uboDynamic.constAlbedo.rgb;
		vec3 blendWeights = pow(abs(geomNorm), vec3(uboConstant.blendSharpness));
		blendWeights = blendWeights / (blendWeights.x + blendWeights.y + blendWeights.z);
		albedo = albedoX * blendWeights.x +
				 albedoY * blendWeights.y +
				 albedoZ * blendWeights.z;
    }
    else
    {
    	albedo = uboDynamic.constAlbedo.rgb;
    }

	vec3 N;
	if (uboDynamic.enableNormalSampler)
	{
		vec3 normalX = ex_TBN * (texture(normalSampler, uvX).xyz * 2 - 1);
		vec3 normalY = ex_TBN * (texture(normalSampler, uvY).xyz * 2 - 1);
		vec3 normalZ = ex_TBN * (texture(normalSampler, uvZ).xyz * 2 - 1);
		vec3 blendWeights = pow(abs(N), vec3(uboConstant.blendSharpness));
		blendWeights = blendWeights / (blendWeights.x + blendWeights.y + blendWeights.z);
		N = normalX * blendWeights.x +
			normalY * blendWeights.y +
			normalZ * blendWeights.z;
		N = normalize(N);
	}
	else
	{
		N = geomNorm;
	}

	float metallic = uboDynamic.enableMetallicSampler ? texture(metallicSampler, ex_TexCoord).r : uboDynamic.constMetallic;
	float roughness = uboDynamic.enableRoughnessSampler ? texture(roughnessSampler, ex_TexCoord).r : uboDynamic.constRoughness;
	float ao = uboDynamic.enableAOSampler ? texture(aoSampler, ex_TexCoord).r : uboDynamic.constAO;

	outPositionMetallic.rgb = ex_WorldPos;
	outPositionMetallic.a = metallic;

	outNormalRoughness.rgb = normalize(N);
	outNormalRoughness.a = roughness;
	
	outAlbedoAO.rgb = albedo;
	outAlbedoAO.a = ao;
}
