
Texture2D<float4> Texture : register(t0);
sampler TextureSampler : register(s0);

#define SAMPLE_COUNT 15

cbuffer VS_BLUR_PARAMETERS : register(c0)
{
    float2 SampleOffsets[SAMPLE_COUNT];
    float SampleWeights[SAMPLE_COUNT];
}

float4 main(float4 color : COLOR0, float2 texCoord : TEXCOORD0) : SV_Target0
{
    float4 c = 0;

    // Combine a number of weighted image filter taps.
    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        c += Texture.Sample(TextureSampler, texCoord + SampleOffsets[i]) * SampleWeights[i];
    }

    return c;
}