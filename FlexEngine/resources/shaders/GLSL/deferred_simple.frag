#version 400

layout (location = 0) out vec3 out_Position;
layout (location = 1) out vec3 out_Normal;
layout (location = 2) out vec4 out_AlbedoSpec;

in vec3 ex_FragPos;
in vec2 ex_TexCoord;
in vec3 ex_Normal;

uniform sampler2D in_DiffuseSampler;
uniform sampler2D in_NormalSampler;
uniform sampler2D in_SpecularSampler;

void main()
{
	// Render to all GBuffers
    out_Position = ex_FragPos;

    vec3 sampledNormal = (texture(in_NormalSampler, ex_TexCoord).xyz * 2 - 1);
    out_Normal = normalize(ex_Normal);
    
    out_AlbedoSpec.rgb = texture(in_DiffuseSampler, ex_TexCoord).rgb;
    
    out_AlbedoSpec.a = texture(in_SpecularSampler, ex_TexCoord).r;
}
