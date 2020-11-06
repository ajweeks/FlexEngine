#version 450 core
layout(location = 0) out vec4 fColour;

layout(set=0, binding=0) uniform sampler2D sTexture;

layout(location = 0) in struct{
    vec4 Colour;
    vec2 UV;
} In;

void main()
{
    fColour = In.Colour * texture(sTexture, In.UV.st);
}
