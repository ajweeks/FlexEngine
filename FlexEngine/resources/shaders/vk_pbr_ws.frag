#version 450

// Deferred PBR - World Space

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 ex_WorldPos;
layout (location = 1) in vec2 ex_TexCoord;
layout (location = 2) in mat3 ex_TBN;

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;

	vec4 constAlbedo;
	float constMetallic;
	float constRoughness;

	bool enableAlbedoSampler;
	bool enableMetallicSampler;
	bool enableRoughnessSampler;
	bool enableNormalSampler;
	
	float blendSharpness;
	float textureScale;
} uboDynamic;

layout (binding = 2) uniform sampler2D albedoSampler;
layout (binding = 3) uniform sampler2D metallicSampler;
layout (binding = 4) uniform sampler2D roughnessSampler;
layout (binding = 5) uniform sampler2D normalSampler;

layout (location = 0) out vec4 outNormalRoughness;
layout (location = 1) out vec4 outAlbedoMetallic;

void main() 
{
    vec2 uvX = ex_WorldPos.zy * uboDynamic.textureScale;
    vec2 uvY = ex_WorldPos.xz * uboDynamic.textureScale;
    vec2 uvZ = ex_WorldPos.xy * uboDynamic.textureScale;

    vec3 geomNorm = ex_TBN[2];

	vec3 albedo;
    if (uboDynamic.enableAlbedoSampler)
    {
		vec3 albedoX = texture(albedoSampler, uvX).rgb * uboDynamic.constAlbedo.rgb;
		vec3 albedoY = texture(albedoSampler, uvY).rgb * uboDynamic.constAlbedo.rgb;
		vec3 albedoZ = texture(albedoSampler, uvZ).rgb * uboDynamic.constAlbedo.rgb;
		vec3 blendWeights = pow(abs(geomNorm), vec3(uboDynamic.blendSharpness));
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
		vec3 blendWeights = pow(abs(geomNorm), vec3(uboDynamic.blendSharpness));
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

	N = normalize(mat3(uboConstant.view) * N);

	outNormalRoughness.rgb = N;
	outNormalRoughness.a = roughness;
	
	outAlbedoMetallic.rgb = albedo;
	outAlbedoMetallic.a = metallic;
}
