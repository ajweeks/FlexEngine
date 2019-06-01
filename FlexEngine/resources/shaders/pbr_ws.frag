#version 400

// Deferred PBR - Triplanar

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 ex_WorldPos;
layout (location = 1) in vec2 ex_TexCoord;
layout (location = 2) in mat3 ex_TBN;

layout (location = 0) out vec4 outNormalRoughness;
layout (location = 1) out vec4 outAlbedoMetallic;

// Material variables
uniform float textureScale = 1.0;
uniform float blendSharpness = 1.0;

uniform vec4 constAlbedo;
uniform bool enableAlbedoSampler;
uniform sampler2D albedoSampler;
uniform float constRoughness;
uniform bool enableRoughnessSampler;
uniform sampler2D roughnessSampler;
uniform float constMetallic;
uniform bool enableMetallicSampler;
uniform sampler2D metallicSampler;
uniform bool enableNormalSampler;
uniform sampler2D normalSampler;

uniform mat4 view;

void main() 
{
    vec2 uvX = ex_WorldPos.zy * textureScale;
    vec2 uvY = ex_WorldPos.xz * textureScale;
    vec2 uvZ = ex_WorldPos.xy * textureScale;

    vec3 geomNorm = ex_TBN[2];

	vec3 albedo;
    if (enableAlbedoSampler)
    {
		vec3 albedoX = texture(albedoSampler, uvX).rgb * constAlbedo.rgb;
		vec3 albedoY = texture(albedoSampler, uvY).rgb * constAlbedo.rgb;
		vec3 albedoZ = texture(albedoSampler, uvZ).rgb * constAlbedo.rgb;
		vec3 blendWeights = pow(abs(geomNorm), vec3(blendSharpness));
		blendWeights = blendWeights / (blendWeights.x + blendWeights.y + blendWeights.z);
		albedo = albedoX * blendWeights.x +
				 albedoY * blendWeights.y +
				 albedoZ * blendWeights.z;
    }
    else
    {
    	albedo = constAlbedo.rgb;
    }

	vec3 N;
	if (enableNormalSampler)
	{
		vec3 normalX = ex_TBN * (texture(normalSampler, uvX).xyz * 2 - 1);
		vec3 normalY = ex_TBN * (texture(normalSampler, uvY).xyz * 2 - 1);
		vec3 normalZ = ex_TBN * (texture(normalSampler, uvZ).xyz * 2 - 1);
		vec3 blendWeights = pow(abs(geomNorm), vec3(blendSharpness));
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

	float metallic = enableMetallicSampler ? texture(metallicSampler, ex_TexCoord).r : constMetallic;
	float roughness = enableRoughnessSampler ? texture(roughnessSampler, ex_TexCoord).r : constRoughness;

	N = normalize(mat3(view) * N);

	outAlbedoMetallic.rgb = albedo;
	outAlbedoMetallic.a = constMetallic;

	outNormalRoughness.rgb = N;
	outNormalRoughness.a = constRoughness;
}
