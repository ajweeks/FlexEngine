#version 400

in vec3 ex_TexCoord;

uniform samplerCube cubemap;

void main() 
{
	gl_FragColor = texture(cubemap, ex_TexCoord);
}
