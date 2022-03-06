
const int SAMPLE_TYPE_CONSTANT = 0;
const int SAMPLE_TYPE_RANDOM = 1;

const int MAX_NUM_PARAMS = 8;

struct Particle
{
	float data[14];
	/*
	vec3 pos;
	vec3 vel;
	vec4 col;
	float lifetime;
	float initial_lifetime;
	vec2 unused;
	*/
};

struct Param
{
	vec4 valueMin;
	vec4 valueMax;
	uint sampleType;
	uint pad0;
	uint pad1;
	uint pad2;
};

struct ParticleSimData
{
	uint bufferOffset;
	uint particleCount;
	float dt;
	uint enableSpawning;
	Param[MAX_NUM_PARAMS] spawnParams;
};

