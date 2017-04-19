
Texture2D<float4> BaseTexture : register(t0);
Texture2D<float4> BloomTexture : register(t1);
sampler TextureSampler : register(s0);

#include "Bloom.hlsli"

// Helper for modifying the saturation of a color.
float4 AdjustSaturation(float4 color, float saturation)
{
    // The constants 0.3, 0.59, and 0.11 are chosen because the
    // human eye is more sensitive to green light, and less to blue.
    float grey = dot(color.rgb, float3(0.3, 0.59, 0.11));

    return lerp(grey, color, saturation);
}

float4 main(float4 color : COLOR0, float2 texCoord : TEXCOORD0) : SV_Target0
{
    float4 base = BaseTexture.Sample(TextureSampler, texCoord);
    float4 bloom = BloomTexture.Sample(TextureSampler, texCoord);

    // Adjust color saturation and intensity.
    bloom = AdjustSaturation(bloom, BloomSaturation) * BloomIntensity;
    base = AdjustSaturation(base, BaseSaturation) * BaseIntensity;
    
    // Darken down the base image in areas where there is a lot of bloom,
    // to prevent things looking excessively burned-out.
    base *= (1 - saturate(bloom));
    
    // Combine the two images.
    return base + bloom;
}