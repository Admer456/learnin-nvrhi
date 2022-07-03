
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
	out float3 outNormal : NORMAL,
	out float2 outTexcoords : TEXCOORD,
	out float3 outColour : COLOR
)
{
	// Wavy little things
	const float waveSizeInverse = 11.0;
	float3 pos = inPosition;

	float4 transformedPos = mul( float4( pos, 1.0 ), mul( viewMatrix, projectionMatrix ) );

	outPosition = transformedPos;
	outTexcoords = inTexcoords;
	outColour = inColour.xyz;
	outNormal = inNormal;
}

Texture2D diffuseTexture : register(t0);
SamplerState diffuseSampler : register(s0);

float HalfLambert( float3 normal, float3 lightDir )
{
	return dot( normal, normalize( lightDir ) ) * 0.5 + 0.5;
}

void main_ps(
	in float4 inPosition : SV_POSITION,
	in float3 inNormal : NORMAL,
	in float2 inTexcoords : TEXCOORD,
	in float3 inColour : COLOR,

	out float4 outColour : SV_TARGET0
)
{
	outColour.rgb = diffuseTexture.Sample( diffuseSampler, inTexcoords ).rgb;// * float3( inTexcoords, 1.0 );
	outColour.rgb *= HalfLambert( inNormal, float3( 20.0, 40.0, 60.0 ) );
	outColour.a = 1.0;
}
