#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 ex_FragPos;
layout (location = 1) in vec2 ex_TexCoord;
layout (location = 2) in vec3 ex_Color;
layout (location = 3) in mat3 ex_TBN;

layout (binding = 1) uniform UBODynamic
{
    mat4 model;
    mat4 modelInvTranspose;
    bool enableDiffuseSampler;
    bool enableNormalSampler;
    bool enableSpecularSampler;
} uboDynamic;

layout (binding = 2) uniform sampler2D in_DiffuseSampler;
layout (binding = 3) uniform sampler2D in_NormalSampler;
layout (binding = 4) uniform sampler2D in_SpecularSampler;

layout (location = 0) out vec3 out_Position;
layout (location = 1) out vec3 out_Normal;
layout (location = 2) out vec4 out_AlbedoSpec;

void main()
{
    // Render to all GBuffers
    out_Position = ex_FragPos;

    if (uboDynamic.enableNormalSampler)
    {
        vec4 normalSample = texture(in_NormalSampler, ex_TexCoord);
        out_Normal = normalize(ex_TBN * (normalSample.xyz * 2 - 1));
    }
    else
    {
        out_Normal = normalize(ex_TBN[2]);
    }
    
    out_AlbedoSpec.rgb = (uboDynamic.enableDiffuseSampler ? texture(in_DiffuseSampler, ex_TexCoord).rgb : vec3(1, 1, 1)) * ex_Color;
    
    out_AlbedoSpec.a = (uboDynamic.enableSpecularSampler ? texture(in_SpecularSampler, ex_TexCoord).r : 1);
}
