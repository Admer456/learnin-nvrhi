
void main_vs(
	float2 inPosition : POSITION,
	float2 inTexcoords : TEXCOORD,

	out float4 outPosition : SV_POSITION,
	out float2 outTexcoords : TEXCOORD
)
{
	outPosition = float4( inPosition, 0.0, 1.0 );
	outTexcoords = inTexcoords;
}

Texture2D screenTexture : register(t0);
Texture2D depthTexture : register(t1);
SamplerState screenSampler : register(s0);

void main_ps(
	in float4 inPosition : SV_POSITION,
	in float2 inTexcoords : TEXCOORD,

	out float4 outColour : SV_TARGET0
)
{
	outColour = screenTexture.Sample( screenSampler, inTexcoords );

	const float depth = depthTexture.Sample( screenSampler, inTexcoords ).x;
	const float distance = clamp( pow( depth, 200.0 ), 0.0, 1.0 );
	
	//outColour.rgb = (outColour.rgb * 2.5) * (1.0 - distance) + float3( 0.54, 0.5, 0.62 ) * distance; 
	outColour.a = 1.0;
}
