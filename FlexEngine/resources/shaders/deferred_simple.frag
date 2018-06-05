#version 400

layout (location = 0) out vec4 out_PositionMetallic;
layout (location = 1) out vec4 out_NormalRoughness;
layout (location = 2) out vec4 out_AlbedoAO;

in vec3 ex_FragPos;
in vec2 ex_TexCoord;
in vec4 ex_Color;
in mat3 ex_TBN;

uniform bool enableDiffuseSampler;
uniform bool enableNormalSampler;

uniform sampler2D diffuseSampler;
uniform sampler2D normalSampler;

void main()
{
	// Render to all GBuffers
    out_PositionMetallic = vec4(ex_FragPos, 0);

	if (enableNormalSampler)
	{
		vec4 normalSample = texture(normalSampler, ex_TexCoord);
		out_NormalRoughness.rgb = normalize(ex_TBN * (normalSample.xyz * 2 - 1));
	}
	else
	{
		out_NormalRoughness.rgb = normalize(ex_TBN[2]);
	}
    
	out_NormalRoughness.a = 0.5f;

    out_AlbedoAO.rgb = (enableDiffuseSampler ? texture(diffuseSampler, ex_TexCoord).rgb : vec3(1, 1, 1)) * ex_Color.rgb;
    
    out_AlbedoAO.a = 1;
}
