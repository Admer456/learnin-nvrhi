
#version 460

layout(binding = 256) uniform ConstantBuffer
{
	mat4 viewMatrix;
	mat4 projectionMatrix;
} constants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexcoord;
layout(location = 3) in vec4 inColour;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec2 fragTexcoord;
layout(location = 2) out vec3 fragColour;

void main()
{
	vec4 position = constants.projectionMatrix 
		* constants.viewMatrix * vec4( inPosition, 1.0 );
	//vec4 position = vec4( inPosition, 1.0 );

	gl_Position = position;
	fragPosition = inPosition;
	fragColour = inColour.rgb;
}
