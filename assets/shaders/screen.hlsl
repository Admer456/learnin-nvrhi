
void main_vs(
	float2 inPosition : POSITION,
	float2 inTexcoords : TEXCOORD,

	out float4 outPosition : SV_POSITION,
	out float2 outTexcoords : TEXCOORD
)
{
	outPosition = float4( inPosition * 0.5, 0.0, 1.0 );
	outTexcoords = inTexcoords;
}

Texture2D screenTexture : register(t0);
SamplerState screenSampler : register(s0);

void main_ps(
	in float4 inPosition : SV_POSITION,
	in float2 inTexcoords : TEXCOORD,

	out float4 outColour : SV_TARGET0
)
{
	outColour = screenTexture.Sample( screenSampler, inTexcoords );
	outColour.xy += inTexcoords * 0.2;
}
