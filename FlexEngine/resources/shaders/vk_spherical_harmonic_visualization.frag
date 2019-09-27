#version 450

layout (location = 0) in mat3 ex_TBN;
layout (location = 3) in vec4 ex_WorldPos;

layout (location = 0) out vec4 fragColor;

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 proj;
} uboConstant;

layout (binding = 0) uniform UBODynamic
{
	mat4 model;

	vec4 cR0;
	vec4 cR1;
	vec4 cG0;
	vec4 cG1;
	vec4 cB0;
	vec4 cB1;
	vec4 cRGB2;
} uboDynamic;

vec3 ComputeProbeRadianceAtPos(vec3 positionWS, vec3 direction)
{
    // Constants
    const float pi = 3.141592653589;
    const float inv_pi = 0.318309886183791;
    const float inv_twopi = 0.159154943091895;
    const float inv_fourpi = 0.0795774715459477;
    const float kInv2SqrtPI = 0.28209479177;
    const float kSqrt3Div2SqrtPI = 0.4886025119;
    const float kSqrt15Div2SqrtPI = 1.09254843059;
    const float k3Sqrt5Div4SqrtPI = 0.94617469576;
    const float kSqrt15Div4SqrtPI = 0.5462742153;
    const float kOneThird = 0.33333333333;

    const int   NUM_COEFFS = 9;

    // Compute UV
    //vec3 texCoord = (positionWS - _DLPBoundsMin) / (_DLPBoundsMax - _DLPBoundsMin);
    //texCoord = texCoord * 2.0 - 1.0;

    // Load the SH coefficients (interpolated at current position)
    // r0 = SAMPLE_TEXTURE3D_LOD(_DLPProbeDataR0, s_trilinear_clamp_sampler, texCoord, 0.0);
    // r1 = SAMPLE_TEXTURE3D_LOD(_DLPProbeDataR1, s_trilinear_clamp_sampler, texCoord, 0.0);
    // g0 = SAMPLE_TEXTURE3D_LOD(_DLPProbeDataG0, s_trilinear_clamp_sampler, texCoord, 0.0);
    // g1 = SAMPLE_TEXTURE3D_LOD(_DLPProbeDataG1, s_trilinear_clamp_sampler, texCoord, 0.0);
    // b0 = SAMPLE_TEXTURE3D_LOD(_DLPProbeDataB0, s_trilinear_clamp_sampler, texCoord, 0.0);
    // b1 = SAMPLE_TEXTURE3D_LOD(_DLPProbeDataB1, s_trilinear_clamp_sampler, texCoord, 0.0);
    // rgb2 = SAMPLE_TEXTURE3D_LOD(_DLPProbeDataRGB2, s_trilinear_clamp_sampler, texCoord, 0.0);

    vec3 coefficients[NUM_COEFFS];
    coefficients[0] = vec3(uboDynamic.cR0[0], uboDynamic.cG0[0], uboDynamic.cB0[0]);
    coefficients[1] = vec3(uboDynamic.cR0[1], uboDynamic.cG0[1], uboDynamic.cB0[1]);
    coefficients[2] = vec3(uboDynamic.cR0[2], uboDynamic.cG0[2], uboDynamic.cB0[2]);
    coefficients[3] = vec3(uboDynamic.cR0[3], uboDynamic.cG0[3], uboDynamic.cB0[3]);
    coefficients[4] = vec3(uboDynamic.cR1[0], uboDynamic.cG1[0], uboDynamic.cB1[0]);
    coefficients[5] = vec3(uboDynamic.cR1[1], uboDynamic.cG1[1], uboDynamic.cB1[1]);
    coefficients[6] = vec3(uboDynamic.cR1[2], uboDynamic.cG1[2], uboDynamic.cB1[2]);
    coefficients[7] = vec3(uboDynamic.cR1[3], uboDynamic.cG1[3], uboDynamic.cB1[3]);
    coefficients[8] = uboDynamic.cRGB2.xyz;

    // Compute the directional factors - I guess these are y (equation 1)
    float x = direction.x; float y = direction.y; float z = direction.z;
    float dirFactors[NUM_COEFFS];
    dirFactors[0] = kInv2SqrtPI;
    dirFactors[1] = -y * kSqrt3Div2SqrtPI;
    dirFactors[2] = z * kSqrt3Div2SqrtPI;
    dirFactors[3] = -x* kSqrt3Div2SqrtPI;
    dirFactors[4] = x * y * kSqrt15Div2SqrtPI;
    dirFactors[5] = -y * z * kSqrt15Div2SqrtPI;
    dirFactors[6] = (z*z - kOneThird) * k3Sqrt5Div4SqrtPI;
    dirFactors[7] = -x * z * kSqrt15Div2SqrtPI;
    dirFactors[8] = (x*x - y*y) * kSqrt15Div4SqrtPI;

    // Reconstruct radiance in normal direction
    vec3 radiance = vec3(0,0,0);
    for (uint i = 0; i < NUM_COEFFS; i++)
    {
        radiance += coefficients[i] * dirFactors[i];
    }
    int i = 5;
    return vec3(mix(vec3(1,0,0), mix(vec3(0,0,0), vec3(0,1,0), max(dirFactors[i], 0)), min(dirFactors[i]+1.0, 1)));
    return radiance;
}

void main()
{
	vec3 N = normalize(mat3(uboConstant.view) * ex_TBN[2]);
	fragColor = vec4(ComputeProbeRadianceAtPos(ex_WorldPos.xyz, N), 1);
	//fragColor = vec4(ex_WorldPos.xyz*.1,1);
	// fragColor = vec4(N*0.5+.5,1);
}
