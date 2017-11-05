#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 ex_FragPos;
layout (location = 1) in vec2 ex_TexCoord;
layout (location = 2) in vec4 ex_Color;
layout (location = 3) in mat3 ex_TBN;

layout (binding = 1) uniform UBODynamic
{
    mat4 model;
    mat4 modelInvTranspose;
    bool enableDiffuseSampler;
    bool enableNormalSampler;
} uboDynamic;

layout (binding = 2) uniform sampler2D in_DiffuseSampler;
layout (binding = 3) uniform sampler2D in_NormalSampler;

layout (location = 0) out vec4 out_PositionMetallic;
layout (location = 1) out vec4 out_NormalRoughness;
layout (location = 2) out vec4 out_AlbedoAO;

void main()
{
    // Render to all GBuffers
    out_PositionMetallic.rgb = ex_FragPos;
    out_PositionMetallic.a = 0;

    if (uboDynamic.enableNormalSampler)
    {
        vec4 normalSample = texture(in_NormalSampler, ex_TexCoord);
        out_NormalRoughness.rgb = normalize(ex_TBN * (normalSample.xyz * 2 - 1));
    }
    else
    {
        out_NormalRoughness.rgb = normalize(ex_TBN[2]);
    }
    
    out_NormalRoughness.a = 0.5f;

    out_AlbedoAO.rgb = (uboDynamic.enableDiffuseSampler ? texture(in_DiffuseSampler, ex_TexCoord).rgb : vec3(1, 1, 1)) * ex_Color.rgb;
    
    out_AlbedoAO.a = 1.0f;
}
