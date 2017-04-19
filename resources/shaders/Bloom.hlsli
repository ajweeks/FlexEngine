
cbuffer VS_BLOOM_PARAMETERS : register(c0)
{
    float BloomThreshold;
    float BlurAmount;
    float BloomIntensity;
    float BaseIntensity;
    float BloomSaturation;
    float BaseSaturation;
}