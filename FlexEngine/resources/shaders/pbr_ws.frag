#version 400

// Deferred PBR - Triplanar

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 ex_WorldPos;
layout (location = 1) in mat3 ex_TBN;
layout (location = 4) in vec4 ex_Color;

layout (location = 0) out vec4 outNormalRoughness;
layout (location = 1) out vec4 outAlbedoMetallic;

// Material variables
uniform float textureScale = 1.0;
uniform float blendSharpness = 1.0;
uniform vec4 constAlbedo;
uniform bool enableAlbedoSampler;
layout (binding = 0) uniform sampler2D albedoSampler;

uniform float constMetallic;
uniform float constRoughness;

uniform bool enableNormalSampler;
layout (binding = 1) uniform sampler2D normalSampler;

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
		vec3 blendWeights = pow(abs(N), vec3(blendSharpness));
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

	outAlbedoMetallic.rgb = albedo * ex_Color.rgb;
	outAlbedoMetallic.a = constMetallic;

	outNormalRoughness.rgb = N;
	outNormalRoughness.a = constRoughness;
}
