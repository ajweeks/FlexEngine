#version 400

layout (location = 0) out vec3 out_Position;
layout (location = 1) out vec3 out_Normal;
layout (location = 2) out vec4 out_AlbedoSpec;

in vec3 ex_FragPos;
in vec2 ex_TexCoord;
in vec3 ex_Color;
in mat3 ex_TBN;

uniform bool enableDiffuseSampler;
uniform bool enableNormalSampler;
uniform bool enableSpecularSampler;

uniform sampler2D diffuseSampler;
uniform sampler2D normalSampler;
uniform sampler2D specularSampler;

void main()
{
	// Render to all GBuffers
    out_Position = ex_FragPos;

	if (enableNormalSampler)
	{
		vec4 normalSample = texture(normalSampler, ex_TexCoord);
		out_Normal = normalize(ex_TBN * (normalSample.xyz * 2 - 1));
	}
	else
	{
		out_Normal = normalize(ex_TBN[2]);
	}
    
    out_AlbedoSpec.rgb = (enableDiffuseSampler ? texture(diffuseSampler, ex_TexCoord).rgb : vec3(1, 1, 1)) * ex_Color;
    
    out_AlbedoSpec.a = (enableSpecularSampler ? texture(specularSampler, ex_TexCoord).r : 1);
}
