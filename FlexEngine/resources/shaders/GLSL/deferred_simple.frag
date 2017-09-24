#version 400

in vec3 ex_FragPos;
in vec2 ex_TexCoord;
in vec3 ex_Color;
in mat3 ex_TBN;

uniform bool in_UseDiffuseSampler;
uniform bool in_UseNormalSampler;
uniform bool in_UseSpecularSampler;

uniform sampler2D in_DiffuseSampler;
uniform sampler2D in_NormalSampler;
uniform sampler2D in_SpecularSampler;

layout (location = 0) out vec3 out_Position;
layout (location = 1) out vec3 out_Normal;
layout (location = 2) out vec4 out_AlbedoSpec;

void main()
{
	// Render to all GBuffers
    out_Position = ex_FragPos;

	if (in_UseNormalSampler)
	{
		vec4 normalSample = texture(in_NormalSampler, ex_TexCoord);
		out_Normal = normalize(ex_TBN * (normalSample.xyz * 2 - 1));
	}
	else
	{
		out_Normal = normalize(ex_TBN[2]);
	}
    
    out_AlbedoSpec.rgb = (in_UseDiffuseSampler ? texture(in_DiffuseSampler, ex_TexCoord).rgb : vec3(1, 1, 1)) * ex_Color;
    
    out_AlbedoSpec.a = (in_UseSpecularSampler ? texture(in_SpecularSampler, ex_TexCoord).r : 1);
}
