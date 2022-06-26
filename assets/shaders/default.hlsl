
#pragma pack_matrix(row_major)

cbuffer CB : register(b0)
{
	float4x4 viewMatrix;
	float4x4 projectionMatrix;
	float time;
}

void main_vs(
	float3 inPosition : POSITION,
	float3 inNormal : NORMAL,
	float2 inTexcoords : TEXCOORD,
	float4 inColour : COLOR,

	out float4 outPosition : SV_POSITION,
	out float2 outTexcoords : TEXCOORD,
	out float3 outColour : COLOR
)
{
	// Wavy little things
	const float waveSizeInverse = 11.0;
	float3 pos = inPosition;
	
	pos.x += sin( time + pos.y * waveSizeInverse ) * 0.0333;
	pos.y += sin( time + pos.x * waveSizeInverse ) * 0.0333;

	pos.z += sin( time + pos.x * waveSizeInverse ) * 0.016;
	pos.z += sin( time + pos.y * waveSizeInverse ) * 0.016;

	float4 transformedPos = mul( float4( pos, 1.0 ), mul( viewMatrix, projectionMatrix ) );

	outPosition = transformedPos;
	outTexcoords = inTexcoords;
	outColour = inColour.xyz;
}

Texture2D diffuseTexture : register(t0);
SamplerState diffuseSampler : register(s0);

void main_ps(
	in float4 inPosition : SV_POSITION,
	in float2 inTexcoords : TEXCOORD,
	in float3 inColour : COLOR,

	out float4 outColour : SV_TARGET0
)
{
	outColour.rgb = diffuseTexture.Sample( diffuseSampler, inTexcoords ).rgb * float3( inTexcoords, 1.0 );
	outColour.a = 1.0;
}
