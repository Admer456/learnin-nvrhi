
#pragma pack_matrix(row_major)

#ifdef SPIRV
#define VK_PUSH_CONSTANT [[vk::push_constant]]
#define VK_BINDING(reg,dset) [[vk::binding(reg,dset)]]
#define VK_DESCRIPTOR_SET(dset) ,space##dset
#else
#define VK_PUSH_CONSTANT
#define VK_BINDING(reg,dset) 
#define VK_DESCRIPTOR_SET(dset)
#endif

cbuffer GlobalBuffer : register(b0)
{
	float4x4 viewMatrix;
	float4x4 projectionMatrix;
	float time;
}

cbuffer RenderSurfaceBuffer : register(b1)
{
	float4x4 entityMatrix;
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
	float4x4 finalMatrix = mul( entityMatrix, mul( viewMatrix, projectionMatrix ) );
	float4 transformedPos = mul( float4( inPosition, 1.0 ), finalMatrix );
	float4 transformedNormal = mul( float4( inNormal, 0.0 ), entityMatrix );

	outPosition = transformedPos;
	outTexcoords = inTexcoords;
	outColour = inColour;
	outNormal = transformedNormal.xyz;
}

SamplerState diffuseSampler : register(s0);
Texture2D diffuseTexture : register(t0 VK_DESCRIPTOR_SET(1));

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
	outColour.rgb = float3( 1.0, 1.0, 1.0 );
	outColour.rgb = diffuseTexture.Sample( diffuseSampler, inTexcoords ).rgb;// * float3( inTexcoords, 1.0 );
	outColour.rgb *= HalfLambert( inNormal, float3( 20.0, 40.0, 60.0 ) );
	outColour.a = 1.0;
}
