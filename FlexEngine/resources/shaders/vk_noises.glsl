
const uint MAX_NUM_ROAD_SEGMENTS = 64;
const uint MAX_NUM_OVERLAPPING_SEGMENTS_PER_CHUNK = 8;
const uint MAX_BIOME_COUNT = 16;
const uint BIOME_NOISE_FUNCTION_INT4_COUNT = 1; // (`Ceil(MAX_BIOME_COUNT / 16)`)
const uint MAX_NUM_NOISE_FUNCTIONS_PER_BIOME = 4;

struct AABB2D
{
    float minX, maxX, minZ, maxZ;
};

struct AABB
{
    // NOTE: Order = X Z Y so we can cast directly to AABB2D
    float minX, maxX, minZ, maxZ, minY, maxY;
};

const uint NOISE_TYPE_PERLIN         = 0;
const uint NOISE_TYPE_FBM            = 1;
const uint NOISE_TYPE_VORONOI        = 2;
const uint NOISE_TYPE_SMOOTH_VORONOI = 3;

struct NoiseFunction
{
    uint type;
    float baseFeatureSize;
    int numOctaves;
    float H; // controls self-similarity [0, 1]
    float heightScale;
    float lacunarity;
    float wavelength;
    float sharpness;
    //int isolateOctave;
    // TODO: Seed
};

struct Biome
{
    NoiseFunction noiseFunctions[MAX_NUM_NOISE_FUNCTIONS_PER_BIOME];
};

struct BezierCurve3D
{
    // TODO: Pack into 12 floats?
    vec4 point0;
    vec4 point1;
    vec4 point2;
    vec4 point3;
};

struct RoadSegment
{
    BezierCurve3D curve;
    AABB aabb;
    float widthStart;
    float widthEnd;
};

struct TerrainGenConstantData
{
    float chunkSize;
    float maxHeight;
    float roadBlendDist;
    float roadBlendThreshold;
    uint vertCountPerChunkAxis;
    int isolateNoiseLayer; // default: -1
    uint biomeCount;
    uint randomTableSize;
    uint numPointsPerAxis;
    float isoLevel;
    float _pad0, _pad1;
    Biome biomes[MAX_BIOME_COUNT];
    NoiseFunction biomeNoise;
    uvec4 biomeNoiseFunctionCounts[BIOME_NOISE_FUNCTION_INT4_COUNT]; // 1 byte per biome func count
    //RoadSegment[MAX_NUM_ROAD_SEGMENTS] roadSegments;
    //int[MAX_NUM_ROAD_SEGMENTS][MAX_NUM_OVERLAPPING_SEGMENTS_PER_CHUNK] overlappingRoadSegmentIndices;
};

struct TerrainGenDynamicData
{
    ivec3 chunkIndex;
    uint linearIndex;
};

uint coordToIndex(uint x, uint y, uint z, uint numPointsPerAxis)
{
    return z * numPointsPerAxis * numPointsPerAxis + y * numPointsPerAxis + x;
}

float hash1(vec2 p)
{
    p = 50.0 * fract(p * 0.3183099);
    return fract(p.x * p.y * (p.x + p.y));
}

float hash1(float n)
{
    return fract(n * 17.0 * fract(n * 0.3183099));
}

vec2 hash2(float n)
{
	return fract(sin(vec2(n, n + 1.0)) * vec2(43758.5453123,22578.1459123));
}

vec2 hash2(vec2 p) 
{
    const vec2 k = vec2(0.3183099, 0.3678794);
    p = p * k + k.yx;
    return fract(16.0 * k * fract(p.x * p.y * (p.x + p.y)));
}

int BiomeIDToindex(vec2 biomeID, uint biomeCount)
{
    float result = hash1(biomeID + vec2(0.12f, 0.258f));
    return int(result * 31.4819f) % int(biomeCount);
}

// Noises from https://www.shadertoy.com/view/4ttSWf

// Value noise, and its analytical derivatives
vec4 noised(vec3 x)
{
    vec3 p = floor(x);
    vec3 w = fract(x);
    vec3 u = w * w * w * (w * (w * 6.0 - 15.0) + 10.0);
    vec3 du = 30.0 * w * w * (w * (w - 2.0) + 1.0);

    float n = p.x + 317.0 * p.y + 157.0 * p.z;
    
    float a = hash1(n + 0.0);
    float b = hash1(n + 1.0);
    float c = hash1(n + 317.0);
    float d = hash1(n + 318.0);
    float e = hash1(n + 157.0);
	float f = hash1(n + 158.0);
    float g = hash1(n + 474.0);
    float h = hash1(n + 475.0);

    float k0 =  a;
    float k1 =  b - a;
    float k2 =  c - a;
    float k3 =  e - a;
    float k4 =  a - b - c + d;
    float k5 =  a - c - e + g;
    float k6 =  a - b - e + f;
    float k7 = -a + b + c - d + e - f - g + h;

    return vec4( -1.0 + 2.0 * (k0 + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x + k7 * u.x * u.y * u.z), 
                      2.0 *  du * vec3(k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z,
                                      k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x,
                                      k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y));
}

float noise(vec3 x)
{
    vec3 p = floor(x);
    vec3 w = fract(x);
    
    vec3 u = w * w * w * (w * (w * 6.0 - 15.0) + 10.0);

    float n = p.x + 317.0 * p.y + 157.0 * p.z;
    
    float a = hash1(n + 0.0);
    float b = hash1(n + 1.0);
    float c = hash1(n + 317.0);
    float d = hash1(n + 318.0);
    float e = hash1(n + 157.0);
	float f = hash1(n + 158.0);
    float g = hash1(n + 474.0);
    float h = hash1(n + 475.0);

    float k0 =  a;
    float k1 =  b - a;
    float k2 =  c - a;
    float k3 =  e - a;
    float k4 =  a - b - c + d;
    float k5 =  a - c - e + g;
    float k6 =  a - b - e + f;
    float k7 = -a + b + c - d + e - f - g + h;

    return -1.0 + 2.0 * (k0 + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x + k7 * u.x * u.y * u.z);
}

vec3 noised(vec2 x)
{
    vec2 p = floor(x);
    vec2 w = fract(x);
    vec2 u = w * w * w * (w * (w * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * w * w * (w * (w - 2.0) + 1.0);
    
    float a = hash1(p + vec2(0, 0));
    float b = hash1(p + vec2(1, 0));
    float c = hash1(p + vec2(0, 1));
    float d = hash1(p + vec2(1, 1));

    float k0 = a;
    float k1 = b - a;
    float k2 = c - a;
    float k4 = a - b - c + d;

    return vec3(-1.0 + 2.0 * (k0 + k1 * u.x + k2 * u.y + k4 * u.x * u.y), 
                       2.0 * du * vec2(k1 + k4 * u.y, k2 + k4 * u.x));
}

float noise(vec2 x)
{
    vec2 p = floor(x);
    vec2 w = fract(x);
    vec2 u = w * w * w * (w * (w * 6.0 - 15.0) + 10.0);

    float a = hash1(p + vec2(0, 0));
    float b = hash1(p + vec2(1, 0));
    float c = hash1(p + vec2(0, 1));
    float d = hash1(p + vec2(1, 1));
    
    return -1.0 + 2.0 * (a + (b - a) * u.x + (c - a) * u.y + (a - b - c + d) * u.x * u.y);
}

// https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
uint pcg_hash(uint n)
{
    uint state = n * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

vec2 Hash2(vec2 p)
{
	const vec2 k = vec2(0.3183099, 0.3678794);
	p = p * k + vec2(k.y, k.x);
	return fract(16.0 * k * fract(p.x * p.y * (p.x + p.y)));
}

// FBM

const mat3 m3  = mat3( 0.00,  0.80,  0.60,
                      -0.80,  0.36, -0.48,
                      -0.60, -0.48,  0.64);
const mat3 m3i = mat3( 0.00, -0.80, -0.60,
                       0.80,  0.36, -0.48,
                       0.60, -0.48,  0.64);
const mat2 m2 = mat2(  0.80,  0.60,
                      -0.60,  0.80);
const mat2 m2i = mat2( 0.80, -0.60,
                       0.60,  0.80);

//------------------------------------------------------------------------------------------

float fbm_4(vec2 x)
{
    float f = 1.9;
    float s = 0.55;
    float a = 0.0;
    float b = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        float n = noise(x);
        a += b * n;
        b *= s;
        x = f * m2 * x;
    }
	return a;
}

float fbm_4(vec3 x)
{
    float f = 2.0;
    float s = 0.5;
    float a = 0.0;
    float b = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        float n = noise(x);
        a += b * n;
        b *= s;
        x = f * m3 * x;
    }
	return a;
}

vec4 fbmd_7(vec3 x)
{
    float f = 1.92;
    float s = 0.5;
    float a = 0.0;
    float b = 0.5;
    vec3  d = vec3(0.0);
    mat3  m = mat3(1.0,0.0,0.0,
                   0.0,1.0,0.0,
                   0.0,0.0,1.0);
    for (int i = 0; i < 7; ++i)
    {
        vec4 n = noised(x);
        a += b * n.x; // accumulate values		
        d += b * m * n.yzw;// accumulate derivatives
        b *= s;
        x = f * m3 * x;
        m = f * m3i * m;
    }
	return vec4(a, d);
}

vec4 fbmd_8(vec3 x)
{
    float f = 2.0;
    float s = 0.65;
    float a = 0.0;
    float b = 0.5;
    vec3  d = vec3(0.0);
    mat3  m = mat3(1.0, 0.0, 0.0,
                   0.0, 1.0, 0.0,
                   0.0, 0.0, 1.0);
    for (int i = 0; i < 8; ++i)
    {
        vec4 n = noised(x);
        a += b * n.x;// accumulate values		
        if (i < 4)
        {
	        d += b * m * n.yzw; // accumulate derivatives
	    }
        b *= s;
        x = f * m3 * x;
        m = f * m3i * m;
    }
	return vec4(a, d);
}

float fbm_9(vec2 x)
{
    float f = 1.9;
    float s = 0.55;
    float a = 0.0;
    float b = 0.5;
    for (int i = 0; i < 9; ++i)
    {
        float n = noise(x);
        a += b * n;
        b *= s;
        x = f * m2 * x;
    }
	return a;
}

float fbm_9(vec2 x, float dampen)
{
    float f = 1.9;
    float s = dampen;
    float a = 0.0;
    float b = 0.5;
    for (int i = 0; i < 9; ++i)
    {
        float n = noise(x);
        a += b * n;
        b *= s;
        x = f * m2 * x;
    }
    return a;
}

vec3 fbmd_9(vec2 x)
{
    float f = 1.9;
    float s = 0.55;
    float a = 0.0;
    float b = 0.5;
    vec2  d = vec2(0.0);
    mat2  m = mat2(1.0, 0.0, 0.0, 1.0);
    for (int i = 0; i < 9; ++i)
    {
        vec3 n = noised(x);
        
        a += b * n.x; // accumulate values		
        d += b * m * n.yz; // accumulate derivatives
        b *= s;
        x = f * m2 * x;
        m = f * m2i * m;
    }
	return vec3(a, d);
}

float VoronoiDistance(vec2 pos, out vec2 outNearestCell, out vec2 outNearestCellPos, out vec2 outSecondNearestCell)
{
	vec2 posCell = floor(pos);
	vec2 posCellF = fract(pos);

	// Regular Voronoi: find cell of nearest point
	vec2 nearestCell = vec2(0.0);
	vec2 nearestCellPos = vec2(0.0);

    vec2 secondNearestCell = vec2(0.0);

	float closestEdge = 8.0;
	for (int j = -1; j <= 1; j++)
	{
		for (int i = -1; i <= 1; i++)
		{
			vec2 cellIndex = vec2(i, j);
			vec2 cellRandomPoint = Hash2(posCell + cellIndex);
			vec2 delta = cellIndex + cellRandomPoint - posCellF;
			float distSq = dot(delta, delta);

			if (distSq < closestEdge)
			{
				closestEdge = distSq;
				nearestCellPos = delta;
				nearestCell = cellIndex;
			}
		}
	}

	// Find distance to nearest edge in any neighboring cell
	closestEdge = 8.0;
	for (int j = -2; j <= 2; j++)
	{
		for (int i = -2; i <= 2; i++)
		{
			vec2 cellIndex = nearestCell + vec2(i, j);
			vec2 cellRandomPoint = Hash2(posCell + cellIndex);
			vec2 delta = cellIndex + cellRandomPoint - posCellF;

			vec2 diff = delta - nearestCellPos;
			float diffLenSq = dot(diff, diff);
			if (diffLenSq > 0.00001)
			{
				float d = dot(0.5 * (nearestCellPos + delta), diff / diffLenSq);
                if (d < closestEdge)
                {
                    secondNearestCell = cellIndex;
                    closestEdge = d;
                }
			}
		}
	}

	outNearestCell = posCell + nearestCell;
    outNearestCellPos = nearestCellPos;
	outSecondNearestCell = secondNearestCell;
	
	// Square distance to the closest edge in the voronoi diagram
	return closestEdge;
}

float VoronoiColumns(vec2 pos, float sharpness, out vec2 outNearestCell, out vec2 outNearestCellPos, out vec2 outSecondNearestCell)
{
	float dist = VoronoiDistance(pos, outNearestCell, outNearestCellPos, outSecondNearestCell);
	return smoothstep(0.0, 1.0 - clamp(sharpness, 0.0, 1.0), dist);
}

float SmoothVoronoi(vec2 pos, float sharpness)
{
    vec2 p = floor(pos);
    vec2  f = fract(pos);

    float res = 0.0;
    for (int j = -1; j <= 1; j++)
    {
        for (int i = -1; i <= 1; i++)
        {
            vec2 b = vec2(i, j);
            vec2 r = vec2(b) - f + Hash2(p + b);
            float d = length(r);

            res += exp(-sharpness * d);
        }
    }
    return -1.0 / sharpness * log(res);
}

// Returns a value in [-1, 1]
float SamplePerlinNoise(sampler2DArray randomTables, uint randomTableSize, vec2 pos, float octave)
{
    const vec2 scaledPos = pos / octave;
    vec2 posi = vec2(float(int(scaledPos.x)), float(int(scaledPos.y)));
    if (scaledPos.x < 0.0)
    {
        posi.x -= 1.0;
    }
    if (scaledPos.y < 0.0)
    {
        posi.y -= 1.0;
    }
    vec2 posf = scaledPos - posi;

    uint tableWidth = uint(sqrt(randomTableSize));
    float texelSize = 1.0 / float(tableWidth);

    //vec2 r00 = int(posi.y * tableWidth + posi.x) % randomTableSize;
    //vec2 r10 = int(posi.y * tableWidth + posi.x + 1) % randomTableSize;
    //vec2 r01 = int((posi.y + 1) * tableWidth + posi.x) % randomTableSize;
    //vec2 r11 = int((posi.y + 1) * tableWidth + posi.x + 1) % randomTableSize;

    // Neighboring random vectors
    vec2 r00 = texture(randomTables, vec3(posi + vec2(0.0, 0.0), octave)).xy;
    vec2 r01 = texture(randomTables, vec3(posi + vec2(texelSize, 0.0), octave)).xy;
    vec2 r10 = texture(randomTables, vec3(posi + vec2(0.0, texelSize), octave)).xy;
    vec2 r11 = texture(randomTables, vec3(posi + vec2(texelSize, texelSize), octave)).xy;

    float r00p = dot(vec2(posf.x, posf.y), r00);
    float r10p = dot(vec2(posf.x-1.0, posf.y), r10);
    float r01p = dot(vec2(posf.x, posf.y-1.0), r01);
    float r11p = dot(vec2(posf.x-1.0, posf.y-1.0), r11);

    float val = mix(mix(r00p, r10p, posf.x), mix(r01p, r11p, posf.x), posf.y);

    return val;
}

// Returns value in range [-1, 1]
float SampleNoiseFunction(sampler2DArray randomTables, uint randomTableSize,
    in NoiseFunction noiseFunction, vec3 pos)
{
    // switch (noiseFunction.type)
    // {
    // case NOISE_TYPE_PERLIN:
    // {
    //     float result = 0.0;

    //     int numOctaves = noiseFunction.numOctaves;
    //     float octave = noiseFunction.baseFeatureSize;
    //     uint octaveIdx = numOctaves - 1;
    //     //int isolateOctave = noiseFunction.isolateOctave;
    //     float heightScale = noiseFunction.heightScale * 0.005; // Normalize value so edited values are reasonable

    //     for (uint i = 0; i < uint(numOctaves); ++i)
    //     {
    //         //if (isolateOctave == -1 || i == uint(isolateOctave))
    //         {
    //             result += SamplePerlinNoise(randomTables, randomTableSize, pos, octave) * octave * heightScale;
    //         }
    //         octave = octave / 2.0;
    //         --octaveIdx;
    //     }

    //     result /= float(numOctaves);

    //     return clamp(result, -1.0, 1.0);
    // } break;
    // case NOISE_TYPE_FBM:
    // {
        vec3 p = pos / noiseFunction.wavelength;
        float n = noise(p.xz); // octave 0 noise
        float dampen = 0.45 + (n * 0.5 + 0.5) * 0.2;
        float result = fbm_9(p.xz, dampen);

        // Separate high areas from low with a cliff edge
        result += 0.25 * smoothstep(-0.056, -0.01, result);
        result *= noiseFunction.heightScale;
        return result;
    // } break;
    // case NOISE_TYPE_VORONOI:
    // {
    //     vec2 p = pos / noiseFunction.wavelength;
    //     vec2 _x;
    //     float result = noiseFunction.heightScale * VoronoiColumns(p, noiseFunction.sharpness, _x, _x, _x);
    //     return result;
    // } break;
    // case NOISE_TYPE_SMOOTH_VORONOI:
    // {
    //     vec2 p = pos / noiseFunction.wavelength;
    //     float result = noiseFunction.heightScale * SmoothVoronoi(p, noiseFunction.sharpness);
    //     return result;
    // } break;
    // }

    return 0.0;
}

float SampleBiomeTerrain(sampler2DArray randomTables, uint randomTableSize, in Biome biome,
    uint biomeNoiseFunctionCount, vec3 posWS)
{
    float result = 0.0;

    for (int i = 0; i < biomeNoiseFunctionCount; ++i)
    {
        //if (chunkData->isolateNoiseLayer == -1 || chunkData->isolateNoiseLayer == i)
        {
            result += SampleNoiseFunction(randomTables, randomTableSize, biome.noiseFunctions[i], posWS);
        }
    }

    result /= float(biomeNoiseFunctionCount);

    return result;
}

uint GetByteFromUInt(uint num, int byteIndex)
{
    return (num >> byteIndex * 8) & 0xFF;
}

uint GetBiomeNoiseFunctionCount(in TerrainGenConstantData constantData, int biomeIndex)
{
    int index0 = biomeIndex / 16;
    int index1 = (biomeIndex % 16) / 4;
    int index2 = (biomeIndex % 16) % 4;
    return GetByteFromUInt(constantData.biomeNoiseFunctionCounts[index0][index1], index2);
}

void GetBiomeIndices(vec3 posWS, in TerrainGenConstantData constantData, out float dist, out int biome0Index, out int biome1Index)
{
    vec2 biome0CellCoord;
    vec2 biome0CellPos;
    vec2 biome1CellCoord;

    vec2 p2D = posWS.xz / constantData.biomeNoise.wavelength;
    dist = VoronoiDistance(p2D, /*out*/ biome0CellCoord, /*out*/ biome0CellPos, /*out*/ biome1CellCoord);
    biome0Index = BiomeIDToindex(biome0CellCoord, constantData.biomeCount);
    biome1Index = BiomeIDToindex(biome1CellCoord, constantData.biomeCount);
}

vec4 SampleTerrain(in TerrainGenConstantData constantData, sampler2DArray randomTables, uint randomTableSize,
    vec3 posWS, out vec3 outNormal)
{
    float dist;
    int biome0Index, biome1Index;
    GetBiomeIndices(posWS, constantData, /*out*/ dist, /*out*/ biome0Index, /*out*/ biome1Index);

    uint biome0NoiseFuncCount = GetBiomeNoiseFunctionCount(constantData, biome0Index);
    float normalizedHeight = SampleBiomeTerrain(randomTables, randomTableSize,
        constantData.biomes[biome0Index], biome0NoiseFuncCount, posWS);

    // Transform range from [-1, 1] to [0, maxHeight]
    float height = (normalizedHeight * 0.5 + 0.5) * constantData.maxHeight;

    float blendWeight = 1.0 - clamp(dist * 5.0 + 0.5, 0.0, 1.0);

    float matID0 = float(biome0Index);
    float matID1 = float(biome1Index);

    float epsilon = 0.5;
    float normalizedHeightX = SampleBiomeTerrain(randomTables, randomTableSize, 
        constantData.biomes[biome0Index], biome0NoiseFuncCount, posWS + vec3(epsilon, 0.0, 0.0));
    float normalizedHeightZ = SampleBiomeTerrain(randomTables, randomTableSize, 
        constantData.biomes[biome0Index], biome0NoiseFuncCount, posWS + vec3(0.0, 0.0, epsilon));

    // TODO: Use proper method for calculating this
    outNormal = normalize(vec3(normalizedHeightX - normalizedHeight, 0.01 * epsilon, normalizedHeightZ - normalizedHeight));

    return vec4(normalizedHeight, blendWeight, matID0, matID1);
}
