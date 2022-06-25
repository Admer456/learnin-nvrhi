
#version 460

layout(binding = 129) uniform sampler2D diffuseTexture;

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec2 fragTexcoord;
layout(location = 2) in vec3 fragColour;

layout(location = 0) out vec4 outColour;

void main()
{
	outColour.rgb = texture( diffuseTexture, fragTexcoord ).rgb;
	//outColour.rgb = fragColour;
	outColour.a = 1.0;
}
