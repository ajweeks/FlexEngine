
float4x4 gWorldViewProj : WORLDVIEWPROJECTION;

struct VS_INPUT 
{
	float3 pos : POSITION;
	float4 color : COLOR;
};

struct VS_OUTPUT 
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
};

VS_OUTPUT MainVS(VS_INPUT input) 
{
	VS_OUTPUT output;
	
	output.pos = mul(float4(input.pos, 1.0f), gWorldViewProj);
	output.color = input.color;

	return output;
}

float4 MainPS(VS_OUTPUT input) : SV_TARGET
{
	return input.color;
}

technique11 Default
{
	pass P0
	{
		SetVertexShader(CompileShader(vs_4_0, MainVS()));
		SetPixelShader(CompileShader(ps_4_0, MainPS()));
	}
}

