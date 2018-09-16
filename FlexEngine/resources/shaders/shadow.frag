#version 400

layout (location = 0) out float fragmentDepth;

void main()
{
	// Not necessary?
	fragmentDepth = gl_FragCoord.z;
}
