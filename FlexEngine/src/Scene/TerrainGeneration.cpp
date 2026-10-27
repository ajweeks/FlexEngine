#include "stdafx.hpp"

#include "Scene/TerrainGeneration.hpp"

#include "Helpers.hpp"
#include "Platform/Platform.hpp"
#include "SaveFile.hpp"
#include "Systems/JobSystem.hpp"

#include <chrono>
#include <fstream>

IGNORE_WARNINGS_PUSH
#include <glm/gtc/type_precision.hpp> // For i64vec3
IGNORE_WARNINGS_POP

// Generation is impractically slow without optimizations, so this file is always optimized
#if defined(DEBUG) && defined(_MSC_VER) && !defined(__clang__)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#pragma auto_inline(on)
#endif

namespace flex
{
	namespace terrain
	{
		// -----------------------------------------------------------------------------------------
		// Noise
		// -----------------------------------------------------------------------------------------

		static inline u32 HashU32(u32 x)
		{
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return x;
		}

		static inline u32 HashCoords(i32 x, i32 y, u32 seed)
		{
			return HashU32((u32)x * 0x8da6b343u + (u32)y * 0xd8163841u + seed * 0xcb1ab31fu);
		}

		static inline u32 HashCoords(i32 x, i32 y, i32 z, u32 seed)
		{
			return HashU32((u32)x * 0x8da6b343u + (u32)y * 0xd8163841u + (u32)z * 0xcb1ab31fu + seed * 0x165667b1u);
		}

		static inline u32 DeriveSeed(u32 seed, u32 purpose)
		{
			return HashU32(seed ^ HashU32(purpose * 0x9E3779B9u + 0x7F4A7C15u));
		}

		static inline real HashToUnit(u32 h)
		{
			return (real)(h & 0xFFFFFF) / (real)0x1000000;
		}

		static inline real Quintic(real t)
		{
			return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
		}

		// NOTE: Hot paths below avoid glm & the STL so they're fast even in unoptimized builds (where calls into
		// templates can't be inlined)

		static inline i32 FastFloor(real x)
		{
			const i32 i = (i32)x;
			return (x < (real)i) ? i - 1 : i;
		}

		static inline real Abs(real x) { return x < 0.0f ? -x : x; }
		static inline real Min(real a, real b) { return a < b ? a : b; }
		static inline real Max(real a, real b) { return a > b ? a : b; }
		static inline real Clamp(real x, real lo, real hi) { return x < lo ? lo : (x > hi ? hi : x); }
		static inline real Lerp(real a, real b, real t) { return a + (b - a) * t; }

		static inline real Smoothstep(real edge0, real edge1, real x)
		{
			const real t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		static const real s_Gradients2D[8][2] =
		{
			{ 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f },
			{ 0.70710678f, 0.70710678f }, { -0.70710678f, 0.70710678f },
			{ 0.70710678f, -0.70710678f }, { -0.70710678f, -0.70710678f },
		};

		static const real s_Gradients3D[16][3] =
		{
			{ 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 },
			{ 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 },
			{ 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 },
			{ 1, 1, 0 }, { -1, 1, 0 }, { 0, -1, 1 }, { 0, -1, -1 },
		};

		// Gradient noise with analytic derivatives (when outDx/outDy are non-null), roughly in [-1, 1]
		static real GradientNoise2(real px, real py, u32 seed, real* outDx = nullptr, real* outDy = nullptr)
		{
			const i32 ix = FastFloor(px);
			const i32 iy = FastFloor(py);
			const real x = px - (real)ix;
			const real y = py - (real)iy;

			const real* g00 = s_Gradients2D[HashCoords(ix, iy, seed) & 7];
			const real* g10 = s_Gradients2D[HashCoords(ix + 1, iy, seed) & 7];
			const real* g01 = s_Gradients2D[HashCoords(ix, iy + 1, seed) & 7];
			const real* g11 = s_Gradients2D[HashCoords(ix + 1, iy + 1, seed) & 7];

			const real n00 = g00[0] * x + g00[1] * y;
			const real n10 = g10[0] * (x - 1.0f) + g10[1] * y;
			const real n01 = g01[0] * x + g01[1] * (y - 1.0f);
			const real n11 = g11[0] * (x - 1.0f) + g11[1] * (y - 1.0f);

			const real u = Quintic(x);
			const real v = Quintic(y);
			const real k = n00 - n10 - n01 + n11;
			const real value = n00 + u * (n10 - n00) + v * (n01 - n00) + u * v * k;

			if (outDx != nullptr)
			{
				const real du = 30.0f * x * x * (x * (x - 2.0f) + 1.0f);
				const real dv = 30.0f * y * y * (y * (y - 2.0f) + 1.0f);
				const real gkx = g00[0] - g10[0] - g01[0] + g11[0];
				const real gky = g00[1] - g10[1] - g01[1] + g11[1];
				*outDx = (g00[0] + u * (g10[0] - g00[0]) + v * (g01[0] - g00[0]) + u * v * gkx + du * ((n10 - n00) + v * k)) * 1.41421356f;
				*outDy = (g00[1] + u * (g10[1] - g00[1]) + v * (g01[1] - g00[1]) + u * v * gky + dv * ((n01 - n00) + u * k)) * 1.41421356f;
			}

			return value * 1.41421356f;
		}

		// Gradient noise, roughly in [-1, 1]
		static real GradientNoise3(real px, real py, real pz, u32 seed)
		{
			const i32 ix = FastFloor(px);
			const i32 iy = FastFloor(py);
			const i32 iz = FastFloor(pz);
			const real x = px - (real)ix;
			const real y = py - (real)iy;
			const real z = pz - (real)iz;

			real n[8];
			for (i32 c = 0; c < 8; ++c)
			{
				const i32 cx = c & 1;
				const i32 cy = (c >> 1) & 1;
				const i32 cz = (c >> 2) & 1;
				const real* g = s_Gradients3D[HashCoords(ix + cx, iy + cy, iz + cz, seed) & 15];
				n[c] = g[0] * (x - (real)cx) + g[1] * (y - (real)cy) + g[2] * (z - (real)cz);
			}

			const real u = Quintic(x);
			const real v = Quintic(y);
			const real w = Quintic(z);

			const real nx00 = Lerp(n[0], n[1], u);
			const real nx10 = Lerp(n[2], n[3], u);
			const real nx01 = Lerp(n[4], n[5], u);
			const real nx11 = Lerp(n[6], n[7], u);
			return Lerp(Lerp(nx00, nx10, v), Lerp(nx01, nx11, v), w);
		}

		// Gradient noise with analytic derivatives (identical values to GradientNoise3)
		static real GradientNoise3D(real px, real py, real pz, u32 seed, real& outDx, real& outDy, real& outDz)
		{
			const i32 ix = FastFloor(px);
			const i32 iy = FastFloor(py);
			const i32 iz = FastFloor(pz);
			const real x = px - (real)ix;
			const real y = py - (real)iy;
			const real z = pz - (real)iz;

			const real* g[8];
			real n[8];
			for (i32 c = 0; c < 8; ++c)
			{
				const i32 cx = c & 1;
				const i32 cy = (c >> 1) & 1;
				const i32 cz = (c >> 2) & 1;
				g[c] = s_Gradients3D[HashCoords(ix + cx, iy + cy, iz + cz, seed) & 15];
				n[c] = g[c][0] * (x - (real)cx) + g[c][1] * (y - (real)cy) + g[c][2] * (z - (real)cz);
			}

			const real u = Quintic(x);
			const real v = Quintic(y);
			const real w = Quintic(z);
			const real du = 30.0f * x * x * (x * (x - 2.0f) + 1.0f);
			const real dv = 30.0f * y * y * (y * (y - 2.0f) + 1.0f);
			const real dw = 30.0f * z * z * (z * (z - 2.0f) + 1.0f);

			// Trilinear blend of the corner values (& of their gradients), plus the derivative of the blend weights
			const real nx00 = Lerp(n[0], n[1], u);
			const real nx10 = Lerp(n[2], n[3], u);
			const real nx01 = Lerp(n[4], n[5], u);
			const real nx11 = Lerp(n[6], n[7], u);
			const real nxy0 = Lerp(nx00, nx10, v);
			const real nxy1 = Lerp(nx01, nx11, v);

			real grad[3];
			for (i32 a = 0; a < 3; ++a)
			{
				const real gx00 = Lerp(g[0][a], g[1][a], u);
				const real gx10 = Lerp(g[2][a], g[3][a], u);
				const real gx01 = Lerp(g[4][a], g[5][a], u);
				const real gx11 = Lerp(g[6][a], g[7][a], u);
				grad[a] = Lerp(Lerp(gx00, gx10, v), Lerp(gx01, gx11, v), w);
			}
			grad[0] += du * Lerp(Lerp(n[1] - n[0], n[3] - n[2], v), Lerp(n[5] - n[4], n[7] - n[6], v), w);
			grad[1] += dv * Lerp(nx10 - nx00, nx11 - nx01, w);
			grad[2] += dw * (nxy1 - nxy0);

			outDx = grad[0];
			outDy = grad[1];
			outDz = grad[2];
			return Lerp(nxy0, nxy1, w);
		}

		// Rotations (& doubling of frequency) applied between octaves to hide lattice alignment
		static inline void NextOctave2(real& x, real& y)
		{
			const real nx = 2.01f * (0.8f * x - 0.6f * y);
			const real ny = 2.01f * (0.6f * x + 0.8f * y);
			x = nx;
			y = ny;
		}

		static inline void NextOctave3(real& x, real& y, real& z)
		{
			const real nx = 2.01f * (-0.80f * y - 0.60f * z);
			const real ny = 2.01f * (0.80f * x + 0.36f * y - 0.48f * z);
			const real nz = 2.01f * (0.60f * x - 0.48f * y + 0.64f * z);
			x = nx;
			y = ny;
			z = nz;
		}

		// Normalized to [-1, 1]
		static real Fbm2(real x, real y, u32 octaves, u32 seed, real gain = 0.5f)
		{
			real sum = 0.0f;
			real amplitude = 1.0f;
			real totalAmplitude = 0.0f;
			for (u32 i = 0; i < octaves; ++i)
			{
				sum += amplitude * GradientNoise2(x, y, seed + i);
				totalAmplitude += amplitude;
				amplitude *= gain;
				NextOctave2(x, y);
			}
			return sum / totalAmplitude;
		}

		// fbm where steep areas receive less high frequency detail, giving an eroded look. Roughly in [-1, 1]
		static real ErodedFbm2(real x, real y, u32 octaves, u32 seed, real gain = 0.5f)
		{
			real sum = 0.0f;
			real amplitude = 1.0f;
			real totalAmplitude = 0.0f;
			real derivativeSumX = 0.0f;
			real derivativeSumY = 0.0f;
			for (u32 i = 0; i < octaves; ++i)
			{
				real dx;
				real dy;
				const real n = GradientNoise2(x, y, seed + i, &dx, &dy);
				derivativeSumX += dx;
				derivativeSumY += dy;
				sum += amplitude * n / (1.0f + derivativeSumX * derivativeSumX + derivativeSumY * derivativeSumY);
				totalAmplitude += amplitude;
				amplitude *= gain;
				NextOctave2(x, y);
			}
			return sum / totalAmplitude * 1.6f;
		}

		// Normalized to [0, 1]
		static real Ridged2(real x, real y, u32 octaves, u32 seed)
		{
			real sum = 0.0f;
			real amplitude = 1.0f;
			real totalAmplitude = 0.0f;
			real prev = 1.0f;
			for (u32 i = 0; i < octaves; ++i)
			{
				real n = 1.0f - Abs(GradientNoise2(x, y, seed + i));
				n = n * n;
				// Higher octaves have more influence on ridge crests than in valleys
				sum += amplitude * n * (0.5f + 0.5f * prev);
				totalAmplitude += amplitude;
				prev = n;
				amplitude *= 0.5f;
				NextOctave2(x, y);
			}
			return sum / totalAmplitude;
		}

		// Normalized to [-1, 1]
		static real Fbm3(real x, real y, real z, u32 octaves, u32 seed)
		{
			real sum = 0.0f;
			real amplitude = 1.0f;
			real totalAmplitude = 0.0f;
			for (u32 i = 0; i < octaves; ++i)
			{
				sum += amplitude * GradientNoise3(x, y, z, seed + i);
				totalAmplitude += amplitude;
				amplitude *= 0.5f;
				NextOctave3(x, y, z);
			}
			return sum / totalAmplitude;
		}

		static inline real SmoothMin(real a, real b, real k)
		{
			const real h = Max(k - Abs(a - b), 0.0f) / k;
			return Min(a, b) - h * h * k * 0.25f;
		}

		static inline real SmoothMax(real a, real b, real k)
		{
			return -SmoothMin(-a, -b, k);
		}


		// Overhang amplitude in mountain massifs
		static const real MASSIF_OVERHANG = 16.0f;
		// Chance of an arch cell containing an arch where arch strength is 1
		static const real ARCH_MAX_CHANCE = 0.3f;

		static inline real Length2(real x, real y)
		{
			return std::sqrt(x * x + y * y);
		}

		static inline real Length3(real x, real y, real z)
		{
			return std::sqrt(x * x + y * y + z * z);
		}

		struct Hasher
		{
			u64 value = 1469598103934665603ull;

			void Bytes(const void* data, size_t size)
			{
				const u8* bytes = (const u8*)data;
				for (size_t i = 0; i < size; ++i)
				{
					value ^= bytes[i];
					value *= 1099511628211ull;
				}
			}

			template<typename T>
			void Add(const T& v)
			{
				Bytes(&v, sizeof(T));
			}
		};

		// -----------------------------------------------------------------------------------------
		// Materials
		// -----------------------------------------------------------------------------------------

		struct MaterialInfo
		{
			const char* name;
			glm::vec3 colour; // sRGB
			glm::vec4 weights; // See SurfaceAttributes
			real vein;
		};

		static const MaterialInfo s_Materials[] =
		{
			{ "Grass", glm::vec3(0.30f, 0.45f, 0.15f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), 0.0f },
			{ "Soil", glm::vec3(0.34f, 0.25f, 0.17f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), 0.0f },
			{ "Rock", glm::vec3(0.42f, 0.40f, 0.38f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), 0.0f },
			{ "Snow", glm::vec3(0.93f, 0.95f, 0.98f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), 0.0f },
			{ "Sand", glm::vec3(0.80f, 0.71f, 0.52f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), 0.0f },
			{ "Granite", glm::vec3(0.60f, 0.57f, 0.56f), glm::vec4(0.0f, 0.25f, 0.75f, 0.0f), 0.0f },
			{ "Sandstone", glm::vec3(0.80f, 0.55f, 0.34f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), 0.0f },
			{ "Marble", glm::vec3(0.90f, 0.89f, 0.86f), glm::vec4(0.0f, 0.0f, 0.3f, 0.0f), 1.0f },
			{ "Basalt", glm::vec3(0.17f, 0.17f, 0.18f), glm::vec4(0.0f, 0.5f, 0.5f, 0.0f), 0.0f },
			{ "Clay", glm::vec3(0.64f, 0.40f, 0.27f), glm::vec4(0.0f, 0.4f, 0.6f, 0.0f), 0.0f },
			{ "Gravel", glm::vec3(0.50f, 0.48f, 0.45f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), 0.0f },
		};
		static_assert(ARRAY_LENGTH(s_Materials) == (u32)Material::_COUNT, "s_Materials must match Material enum");

		// Points well below the heightfield are in caves (the heightfield is where the surface would be without caves,
		// give or take overhangs), which sunlight & the sky don't reach
		static real SkyVisibility(const glm::vec3& pos, const ColumnSample& column)
		{
			const real depth = column.height - pos.y;
			const real start = column.overhangAmplitude * 0.8f + 6.0f;
			const real t = glm::clamp((depth - start) / 24.0f, 0.0f, 1.0f);
			return 1.0f - t * t * (3.0f - 2.0f * t);
		}

		// Texture weights are normalized so snow can be implied, freeing w for sky visibility
		static glm::vec4 PackVertexWeights(const glm::vec4& weights, real skyVisibility)
		{
			const glm::vec4 clamped = glm::max(weights, glm::vec4(0.0f));
			const real sum = clamped.x + clamped.y + clamped.z + clamped.w;
			const glm::vec3 normalized = sum > 1e-5f ? glm::vec3(clamped) / sum : glm::vec3(0.0f, 1.0f, 0.0f);
			return glm::vec4(normalized, skyVisibility);
		}

		const char* MaterialName(Material material)
		{
			return (u32)material < (u32)Material::_COUNT ? s_Materials[(u32)material].name : "Invalid";
		}

		static const char* s_VegetationNames[] =
		{
			"none", "taiga", "nordic", "broadleaf", "swamp", "tundra", "alpine", "savanna", "desert", "volcanic",
		};
		static_assert(ARRAY_LENGTH(s_VegetationNames) == (u32)Vegetation::_COUNT, "s_VegetationNames must match Vegetation enum");

		const char* VegetationName(Vegetation vegetation)
		{
			return (u32)vegetation < (u32)Vegetation::_COUNT ? s_VegetationNames[(u32)vegetation] : "none";
		}

		Vegetation ParseVegetation(const std::string& name, Vegetation fallback)
		{
			for (u32 i = 0; i < (u32)Vegetation::_COUNT; ++i)
			{
				if (name == s_VegetationNames[i])
				{
					return (Vegetation)i;
				}
			}
			return fallback;
		}

		// -----------------------------------------------------------------------------------------
		// World params
		// -----------------------------------------------------------------------------------------

		enum SeedPurpose : u32
		{
			SEED_BIOME_WARP_X = 1,
			SEED_BIOME_WARP_Z,
			SEED_BIOME_CELLS,
			SEED_CONTINENT,
			SEED_OVERHANG,
			SEED_ARCH_CELLS,
			SEED_ARCH_NOISE,
			SEED_LAKE_CELLS,
			SEED_LAKE_SHORE,
			SEED_LAKE_BED,
			SEED_CAVE_0,
			SEED_CAVE_1,
			SEED_CAVE_MASK,
			SEED_COLOUR,
			SEED_MASSIF_CELLS,
			SEED_MASSIF_WARP,
			SEED_MASSIF_RIDGES,
			SEED_MASSIF_DETAIL,
			SEED_CAVE_ENTRANCE,
			SEED_CLIMATE,
			SEED_OCEAN,
			SEED_OCEAN_WARP,
			SEED_OCEAN_FLOOR,
			SEED_TUNNEL_SIZE,
			SEED_CAVERN,
			SEED_TREES,
			SEED_FOREST,
			SEED_TREASURE,
			SEED_BUSHES,
			SEED_SPAWN,
			SEED_BIOME_BASE = 1000,
		};

		WorldParams WorldParams::Default()
		{
			WorldParams params = {};

			// Colours are sRGB albedos, kept fairly muted (real grass & rock are darker & less saturated than they appear)

			BiomeParams plains = {};
			plains.name = "Plains";
			plains.frequency = 1.0f;
			plains.temperature = 0.1f;
			plains.baseHeight = 10.0f;
			plains.heightAmplitude = 70.0f;
			plains.wavelength = 1400.0f;
			plains.caveStrength = 0.6f;
			plains.lakeStrength = 1.0f;
			plains.groundColour = glm::vec3(0.30f, 0.35f, 0.18f);
			plains.cliffColour = glm::vec3(0.42f, 0.38f, 0.32f);
			plains.grassiness = 1.0f;
			plains.vegetation = Vegetation::BROADLEAF;
			plains.treeDensity = 0.8f;
			params.biomes.push_back(plains);

			BiomeParams hills = {};
			hills.name = "Hills";
			hills.frequency = 1.0f;
			hills.temperature = -0.05f;
			hills.baseHeight = 60.0f;
			hills.heightAmplitude = 260.0f;
			hills.wavelength = 1100.0f;
			hills.ridgedness = 0.25f;
			hills.overhangAmplitude = 10.0f;
			hills.caveStrength = 1.0f;
			hills.lakeStrength = 0.8f;
			hills.groundColour = glm::vec3(0.27f, 0.32f, 0.17f);
			hills.cliffColour = glm::vec3(0.40f, 0.38f, 0.35f);
			hills.grassiness = 1.0f;
			hills.vegetation = Vegetation::NORDIC;
			params.biomes.push_back(hills);

			BiomeParams mountains = {};
			mountains.name = "Mountains";
			mountains.frequency = 0.9f;
			mountains.temperature = -0.25f;
			mountains.baseHeight = 250.0f;
			mountains.heightAmplitude = 1100.0f;
			mountains.wavelength = 2600.0f;
			mountains.ridgedness = 0.85f;
			mountains.overhangAmplitude = 22.0f;
			mountains.caveStrength = 0.8f;
			mountains.lakeStrength = 0.6f;
			mountains.groundColour = glm::vec3(0.29f, 0.31f, 0.21f);
			mountains.cliffColour = glm::vec3(0.38f, 0.37f, 0.37f);
			mountains.peakColour = glm::vec3(0.92f, 0.94f, 0.97f);
			mountains.peakHeight = 650.0f;
			mountains.grassiness = 0.8f;
			mountains.vegetation = Vegetation::TAIGA;
			mountains.treeDensity = 0.8f;
			mountains.bushDensity = 0.8f;
			params.biomes.push_back(mountains);

			BiomeParams mesa = {};
			mesa.name = "Mesa";
			mesa.frequency = 0.8f;
			mesa.temperature = 0.65f;
			mesa.baseHeight = 70.0f;
			mesa.heightAmplitude = 340.0f;
			mesa.wavelength = 1300.0f;
			mesa.ridgedness = 0.1f;
			mesa.terraceStrength = 0.9f;
			mesa.terraceHeight = 55.0f;
			mesa.overhangAmplitude = 14.0f;
			mesa.archStrength = 0.5f;
			mesa.caveStrength = 0.5f;
			mesa.lakeStrength = 0.2f;
			mesa.groundColour = glm::vec3(0.66f, 0.46f, 0.29f);
			mesa.cliffColour = glm::vec3(0.62f, 0.33f, 0.18f);
			mesa.grassiness = 0.1f;
			mesa.vegetation = Vegetation::DESERT;
			mesa.treeDensity = 0.7f;
			params.biomes.push_back(mesa);

			BiomeParams badlands = {};
			badlands.name = "Badlands";
			badlands.frequency = 0.6f;
			badlands.temperature = 0.55f;
			badlands.baseHeight = 40.0f;
			badlands.heightAmplitude = 180.0f;
			badlands.wavelength = 900.0f;
			badlands.ridgedness = 0.5f;
			badlands.overhangAmplitude = 45.0f;
			badlands.caveStrength = 1.0f;
			badlands.lakeStrength = 0.3f;
			badlands.groundColour = glm::vec3(0.52f, 0.47f, 0.39f);
			badlands.cliffColour = glm::vec3(0.46f, 0.41f, 0.37f);
			badlands.grassiness = 0.15f;
			badlands.vegetation = Vegetation::SAVANNA;
			params.biomes.push_back(badlands);

			BiomeParams lowlands = {};
			lowlands.name = "Lowlands";
			lowlands.frequency = 0.7f;
			lowlands.temperature = 0.3f;
			lowlands.baseHeight = -60.0f;
			lowlands.heightAmplitude = 40.0f;
			lowlands.wavelength = 1800.0f;
			lowlands.caveStrength = 0.3f;
			lowlands.lakeStrength = 1.0f;
			lowlands.groundColour = glm::vec3(0.36f, 0.36f, 0.24f);
			lowlands.cliffColour = glm::vec3(0.36f, 0.34f, 0.29f);
			lowlands.grassiness = 0.85f;
			lowlands.vegetation = Vegetation::BROADLEAF;
			lowlands.treeDensity = 1.1f;
			lowlands.bushDensity = 1.3f;
			params.biomes.push_back(lowlands);

			// Cold, gently rolling ground with patchy snow & lots of small lakes
			BiomeParams tundra = {};
			tundra.name = "Tundra";
			tundra.frequency = 0.8f;
			tundra.temperature = -0.85f;
			tundra.baseHeight = 30.0f;
			tundra.heightAmplitude = 70.0f;
			tundra.wavelength = 1500.0f;
			tundra.ridgedness = 0.15f;
			tundra.caveStrength = 0.4f;
			tundra.lakeStrength = 1.0f;
			tundra.groundColour = glm::vec3(0.40f, 0.38f, 0.29f);
			tundra.cliffColour = glm::vec3(0.40f, 0.40f, 0.41f);
			tundra.peakColour = glm::vec3(0.90f, 0.92f, 0.95f);
			tundra.peakHeight = 70.0f;
			tundra.grassiness = 0.55f;
			tundra.vegetation = Vegetation::TUNDRA;
			params.biomes.push_back(tundra);

			// Cold hills, darker & greener than tundra
			BiomeParams boreal = {};
			boreal.name = "Boreal";
			boreal.frequency = 0.8f;
			boreal.temperature = -0.5f;
			boreal.baseHeight = 50.0f;
			boreal.heightAmplitude = 160.0f;
			boreal.wavelength = 1300.0f;
			boreal.ridgedness = 0.2f;
			boreal.overhangAmplitude = 6.0f;
			boreal.caveStrength = 0.8f;
			boreal.lakeStrength = 0.9f;
			boreal.groundColour = glm::vec3(0.22f, 0.27f, 0.15f);
			boreal.cliffColour = glm::vec3(0.37f, 0.37f, 0.36f);
			boreal.peakColour = glm::vec3(0.91f, 0.93f, 0.96f);
			boreal.peakHeight = 260.0f;
			boreal.grassiness = 0.9f;
			boreal.vegetation = Vegetation::TAIGA;
			boreal.treeDensity = 1.2f;
			params.biomes.push_back(boreal);

			// Sand dunes (sharp ridged crests)
			BiomeParams desert = {};
			desert.name = "Desert";
			desert.frequency = 0.8f;
			desert.temperature = 0.9f;
			desert.baseHeight = 20.0f;
			desert.heightAmplitude = 55.0f;
			desert.wavelength = 650.0f;
			desert.ridgedness = 0.7f;
			desert.caveStrength = 0.2f;
			desert.lakeStrength = 0.05f;
			desert.groundColour = glm::vec3(0.78f, 0.66f, 0.48f);
			desert.cliffColour = glm::vec3(0.70f, 0.52f, 0.36f);
			desert.grassiness = 0.0f;
			desert.vegetation = Vegetation::DESERT;
			params.biomes.push_back(desert);

			// Stepped sandstone plateaus, cut by canyons, with natural arches
			BiomeParams canyons = {};
			canyons.name = "Sandstone canyons";
			canyons.frequency = 0.6f;
			canyons.temperature = 0.75f;
			canyons.baseHeight = 120.0f;
			canyons.heightAmplitude = 260.0f;
			canyons.wavelength = 1000.0f;
			canyons.ridgedness = 0.35f;
			canyons.terraceStrength = 0.7f;
			canyons.terraceHeight = 28.0f;
			canyons.overhangAmplitude = 18.0f;
			canyons.archStrength = 1.0f;
			canyons.caveStrength = 0.7f;
			canyons.lakeStrength = 0.1f;
			canyons.groundColour = glm::vec3(0.72f, 0.52f, 0.35f);
			canyons.cliffColour = glm::vec3(0.76f, 0.47f, 0.28f);
			canyons.grassiness = 0.05f;
			canyons.vegetation = Vegetation::DESERT;
			canyons.treeDensity = 0.6f;
			canyons.bushDensity = 0.8f;
			params.biomes.push_back(canyons);

			// Dark basalt ridges
			BiomeParams volcanic = {};
			volcanic.name = "Volcanic";
			volcanic.frequency = 0.35f;
			volcanic.temperature = 0.35f;
			volcanic.baseHeight = 90.0f;
			volcanic.heightAmplitude = 420.0f;
			volcanic.wavelength = 1700.0f;
			volcanic.ridgedness = 0.75f;
			volcanic.overhangAmplitude = 16.0f;
			volcanic.caveStrength = 1.0f;
			volcanic.lakeStrength = 0.2f;
			volcanic.groundColour = glm::vec3(0.24f, 0.23f, 0.21f);
			volcanic.cliffColour = glm::vec3(0.17f, 0.17f, 0.18f);
			volcanic.grassiness = 0.2f;
			volcanic.vegetation = Vegetation::VOLCANIC;
			params.biomes.push_back(volcanic);

			// Very flat, low & full of lakes
			BiomeParams wetlands = {};
			wetlands.name = "Wetlands";
			wetlands.frequency = 0.5f;
			wetlands.temperature = 0.2f;
			wetlands.baseHeight = 5.0f;
			wetlands.heightAmplitude = 16.0f;
			wetlands.wavelength = 900.0f;
			wetlands.caveStrength = 0.1f;
			wetlands.lakeStrength = 1.0f;
			wetlands.groundColour = glm::vec3(0.26f, 0.30f, 0.17f);
			wetlands.cliffColour = glm::vec3(0.33f, 0.30f, 0.25f);
			wetlands.grassiness = 1.0f;
			wetlands.vegetation = Vegetation::SWAMP;
			params.biomes.push_back(wetlands);

			return params;
		}

		u64 WorldParams::ContentHash() const
		{
			Hasher hasher;
			hasher.Add(GENERATOR_VERSION);
			hasher.Add(seed);
			hasher.Add(voxelSize);
			hasher.Add(cellsPerChunkAxis);
			hasher.Add(biomeCellSize);
			hasher.Add(biomeBlendDistance);
			hasher.Add(continentWavelength);
			hasher.Add(continentAmplitude);
			hasher.Add(overhangWavelength);
			hasher.Add(archCellSize);
			hasher.Add(archMinRadius);
			hasher.Add(archMaxRadius);
			hasher.Add(caveWavelength);
			hasher.Add(tunnelRadius);
			hasher.Add(caveMaxDepth);
			hasher.Add(cavernWavelength);
			hasher.Add(cavernChance);
			hasher.Add(lakeCellSize);
			hasher.Add(lakeChance);
			hasher.Add(lakeMinRadius);
			hasher.Add(lakeMaxRadius);
			hasher.Add(lakeMaxRelief);
			hasher.Add(massifCellSize);
			hasher.Add(massifChance);
			hasher.Add(massifMinHeight);
			hasher.Add(massifMaxHeight);
			hasher.Add(climateWavelength);
			hasher.Add(seaLevel);
			hasher.Add(oceanWavelength);
			hasher.Add(oceanCoverage);
			hasher.Add(oceanDepth);
			for (const BiomeParams& biome : biomes)
			{
				hasher.Add(biome.frequency);
				hasher.Add(biome.temperature);
				hasher.Add(biome.baseHeight);
				hasher.Add(biome.heightAmplitude);
				hasher.Add(biome.wavelength);
				hasher.Add(biome.ridgedness);
				hasher.Add(biome.terraceStrength);
				hasher.Add(biome.terraceHeight);
				hasher.Add(biome.overhangAmplitude);
				hasher.Add(biome.archStrength);
				hasher.Add(biome.caveStrength);
				hasher.Add(biome.lakeStrength);
				hasher.Add(biome.groundColour);
				hasher.Add(biome.cliffColour);
				hasher.Add(biome.peakColour);
				hasher.Add(biome.peakHeight);
				hasher.Add(biome.grassiness);
				hasher.Add((u32)biome.vegetation);
				hasher.Add(biome.treeDensity);
				hasher.Add(biome.bushDensity);
			}
			return hasher.value;
		}

		// -----------------------------------------------------------------------------------------
		// Density
		// -----------------------------------------------------------------------------------------

		static std::atomic<u32> s_NextDensityInstanceID(1);

		// Per thread caches of values derived from expensive heightfield queries (lake levels & arch base heights)
		struct LakeCacheEntry
		{
			u32 instanceID;
			i32 cellX;
			i32 cellZ;
			bool bValid;
			real level;
		};

		struct ArchCacheEntry
		{
			u32 instanceID;
			i32 cellX;
			i32 cellZ;
			bool bValid;
			real baseHeight;
		};

		struct BiomeCellCacheEntry
		{
			u32 instanceID;
			i32 cellX;
			i32 cellZ;
			i32 biome;
		};

		static const u32 FEATURE_CACHE_SIZE = 256;
		static thread_local LakeCacheEntry t_LakeCache[FEATURE_CACHE_SIZE];
		static thread_local ArchCacheEntry t_ArchCache[FEATURE_CACHE_SIZE];
		static thread_local BiomeCellCacheEntry t_BiomeCellCache[FEATURE_CACHE_SIZE];

		// Width of the climate range each biome appears in
		static const real BIOME_TEMPERATURE_RANGE = 0.45f;
		// Ocean weight past which the sea surface is shown (& below which coastal land is raised above sea level)
		static const real OCEAN_SHORE_WEIGHT = 0.08f;

		TerrainDensity::TerrainDensity(const WorldParams& params) :
			m_Params(params),
			m_ColumnCache(new ColumnGridCache())
		{
			if (m_Params.biomes.empty())
			{
				m_Params.biomes = WorldParams::Default().biomes;
			}
			if (m_Params.biomes.size() > MAX_BIOME_COUNT)
			{
				m_Params.biomes.resize(MAX_BIOME_COUNT);
			}

			m_Params.voxelSize = glm::max(m_Params.voxelSize, 0.05f);
			m_Params.biomeCellSize = glm::max(m_Params.biomeCellSize, 1.0f);
			m_Params.biomeBlendDistance = glm::max(m_Params.biomeBlendDistance, 0.01f);
			m_Params.continentWavelength = glm::max(m_Params.continentWavelength, 1.0f);
			m_Params.overhangWavelength = glm::max(m_Params.overhangWavelength, 1.0f);
			m_Params.archCellSize = glm::max(m_Params.archCellSize, 10.0f);
			m_Params.archMinRadius = glm::max(m_Params.archMinRadius, 1.0f);
			// Arches must fit within their cell (only the containing cell is checked)
			m_Params.archMaxRadius = glm::clamp(m_Params.archMaxRadius, m_Params.archMinRadius, m_Params.archCellSize * 0.2f);
			m_Params.caveWavelength = glm::max(m_Params.caveWavelength, 1.0f);
			m_Params.tunnelRadius = glm::max(m_Params.tunnelRadius, 0.0f);
			m_Params.cavernWavelength = glm::max(m_Params.cavernWavelength, 1.0f);
			m_Params.cavernChance = glm::clamp(m_Params.cavernChance, 0.0f, 1.0f);
			m_Params.climateWavelength = glm::max(m_Params.climateWavelength, 1.0f);
			m_Params.oceanWavelength = glm::max(m_Params.oceanWavelength, 100.0f);
			m_Params.oceanCoverage = glm::clamp(m_Params.oceanCoverage, 0.0f, 1.0f);
			m_Params.oceanDepth = glm::max(m_Params.oceanDepth, 0.0f);
			m_Params.lakeCellSize = glm::max(m_Params.lakeCellSize, 10.0f);
			m_Params.lakeMinRadius = glm::max(m_Params.lakeMinRadius, 1.0f);
			m_Params.lakeMaxRadius = glm::max(m_Params.lakeMaxRadius, m_Params.lakeMinRadius);
			m_Params.massifCellSize = glm::max(m_Params.massifCellSize, 100.0f);
			m_Params.massifMaxHeight = glm::max(m_Params.massifMaxHeight, m_Params.massifMinHeight);

			m_Seed = HashU32((u32)m_Params.seed);
			m_InstanceID = s_NextDensityInstanceID.fetch_add(1);

			m_TotalBiomeFrequency = 0.0f;
			real maxOverhang = 0.0f;
			bool bAnyArches = false;
			bool bAnyCaves = false;
			m_BiomeOctaves.resize(m_Params.biomes.size());
			for (u32 i = 0; i < (u32)m_Params.biomes.size(); ++i)
			{
				BiomeParams& biome = m_Params.biomes[i];
				biome.frequency = glm::max(biome.frequency, 0.0f);
				biome.wavelength = glm::max(biome.wavelength, 1.0f);
				biome.terraceHeight = glm::max(biome.terraceHeight, 0.1f);
				biome.grassiness = glm::clamp(biome.grassiness, 0.0f, 1.0f);
				biome.treeDensity = glm::max(biome.treeDensity, 0.0f);
				biome.bushDensity = glm::max(biome.bushDensity, 0.0f);
				if ((u32)biome.vegetation >= (u32)Vegetation::_COUNT)
				{
					biome.vegetation = Vegetation::NONE;
				}
				m_TotalBiomeFrequency += biome.frequency;
				maxOverhang = glm::max(maxOverhang, biome.overhangAmplitude);
				bAnyArches = bAnyArches || biome.archStrength > 0.0f;
				bAnyCaves = bAnyCaves || biome.caveStrength > 0.0f;

				// Add octaves until features are smaller than a level 0 cell
				m_BiomeOctaves[i] = (u32)glm::clamp((i32)glm::ceil(glm::log2(biome.wavelength / m_Params.voxelSize)), 1, 12);
			}
			if (m_TotalBiomeFrequency <= 0.0f)
			{
				for (BiomeParams& biome : m_Params.biomes)
				{
					biome.frequency = 1.0f;
				}
				m_TotalBiomeFrequency = (real)m_Params.biomes.size();
			}

			m_MassifOctaves = (u32)glm::clamp((i32)glm::ceil(glm::log2(3600.0f / m_Params.voxelSize)), 1, 14);

			// Find the ocean noise value which the requested fraction of the world lies below
			m_OceanThreshold = -1e30f;
			if (m_Params.oceanCoverage > 0.0f)
			{
				const i32 gridSize = 48;
				std::vector<real> values;
				values.reserve(gridSize * gridSize);
				for (i32 z = 0; z < gridSize; ++z)
				{
					for (i32 x = 0; x < gridSize; ++x)
					{
						// Spaced far enough apart to be roughly independent samples
						values.push_back(OceanNoise((real)x * 1.37f + 0.31f, (real)z * 1.37f + 0.77f));
					}
				}
				std::sort(values.begin(), values.end());
				m_OceanThreshold = m_Params.oceanCoverage >= 1.0f ? 1e30f : values[(size_t)(m_Params.oceanCoverage * (real)(values.size() - 1))];
			}
			if (m_Params.massifChance > 0.0f)
			{
				maxOverhang = glm::max(maxOverhang, MASSIF_OVERHANG);
			}

			const real margin = m_Params.voxelSize * 2.0f;
			const real archExtent = m_Params.archMaxRadius * 1.0f + m_Params.archMaxRadius * 0.4f + 6.0f;
			m_MaxExtentAbove = maxOverhang * 1.5f + (bAnyArches ? archExtent : 0.0f) + margin;
			m_MaxExtentBelow = glm::max(maxOverhang * 1.5f, bAnyCaves ? m_Params.caveMaxDepth : 0.0f) + margin;
		}

		real TerrainDensity::Temperature(real x, real z) const
		{
			const real inv = 1.0f / m_Params.climateWavelength;
			return Clamp(Fbm2(x * inv, z * inv, 3, DeriveSeed(m_Seed, SEED_CLIMATE)) * 2.2f, -1.0f, 1.0f);
		}

		real TerrainDensity::OceanNoise(real u, real v) const
		{
			// (u, v) are in units of the ocean wavelength. Domain warping gives more varied coastlines.
			const u32 warpSeed = DeriveSeed(m_Seed, SEED_OCEAN_WARP);
			const real wu = Fbm2(u * 1.7f, v * 1.7f, 3, warpSeed) * 0.35f;
			const real wv = Fbm2(u * 1.7f + 4.3f, v * 1.7f + 9.1f, 3, warpSeed + 3) * 0.35f;
			return Fbm2(u + wu, v + wv, 5, DeriveSeed(m_Seed, SEED_OCEAN));
		}

		real TerrainDensity::OceanWeight(real x, real z) const
		{
			if (m_Params.oceanCoverage <= 0.0f)
			{
				return 0.0f;
			}
			const real inv = 1.0f / m_Params.oceanWavelength;
			return Smoothstep(0.0f, 0.2f, m_OceanThreshold - OceanNoise(x * inv, z * inv));
		}

		void TerrainDensity::ApplyOcean(real x, real z, ColumnSample& column) const
		{
			const real m = OceanWeight(x, z);
			column.oceanWeight = m;
			if (m <= 0.0f)
			{
				return;
			}

			const real seaLevel = m_Params.seaLevel;

			// Raise low land near the coast so the sea is always contained (e.g. lowlands below sea level)
			real height = Lerp(column.height, Max(column.height, seaLevel + 3.0f), Smoothstep(0.0f, OCEAN_SHORE_WEIGHT, m));

			// Then slope down to the sea floor
			const real floorNoise = Fbm2(x / 900.0f, z / 900.0f, 3, DeriveSeed(m_Seed, SEED_OCEAN_FLOOR));
			const real seaFloor = seaLevel - 6.0f - m_Params.oceanDepth * Smoothstep(0.15f, 0.9f, m) + 18.0f * floorNoise;
			const real blend = Smoothstep(OCEAN_SHORE_WEIGHT, 0.45f, m);
			column.height = Lerp(height, seaFloor, blend);

			const real keep = 1.0f - Smoothstep(0.05f, 0.3f, m);
			column.caveStrength *= keep;
			column.cavernStrength *= keep;
			column.archStrength *= keep;
			column.overhangAmplitude *= 1.0f - Smoothstep(0.2f, 0.5f, m);
			column.lakeStrength *= 1.0f - Smoothstep(0.0f, 0.02f, m);
			column.grassiness *= 1.0f - blend;
			column.groundColour = glm::mix(column.groundColour, glm::vec3(0.56f, 0.51f, 0.40f), blend);

			// The sea surface is marked using the lake fields (lakeT < 1: under water)
			column.lakeID = OCEAN_ID;
			column.waterLevel = seaLevel;
			column.lakeT = Clamp(1.0f - (m - OCEAN_SHORE_WEIGHT) * 10.0f, -1.0f, 2.0f);
		}

		real TerrainDensity::BiomeHeight(u32 biomeIndex, real x, real z) const
		{
			const BiomeParams& biome = m_Params.biomes[biomeIndex];
			const real invWavelength = 1.0f / biome.wavelength;
			const real px = x * invWavelength;
			const real pz = z * invWavelength;
			const u32 seed = DeriveSeed(m_Seed, SEED_BIOME_BASE + biomeIndex * 16);
			const u32 octaves = m_BiomeOctaves[biomeIndex];

			real h = 0.0f;
			if (biome.ridgedness < 1.0f)
			{
				h += (1.0f - biome.ridgedness) * ErodedFbm2(px, pz, octaves, seed, 0.47f);
			}
			if (biome.ridgedness > 0.0f)
			{
				// Shift ridged noise so its mean sits around zero
				h += biome.ridgedness * (Ridged2(px, pz, octaves, seed + 100) * 2.0f - 0.7f) * (0.75f + 0.25f * ErodedFbm2(px * 0.5f, pz * 0.5f, 4, seed + 200));
			}

			real height = biome.baseHeight + biome.heightAmplitude * h;

			if (biome.terraceStrength > 0.0f)
			{
				const real t = height / biome.terraceHeight;
				const real floorT = (real)FastFloor(t);
				const real stepped = (floorT + Smoothstep(0.7f, 1.0f, t - floorT)) * biome.terraceHeight;
				height = Lerp(height, stepped, biome.terraceStrength);
			}

			return height;
		}

		real TerrainDensity::MassifHeight(real x, real z, real& outWeight) const
		{
			outWeight = 0.0f;
			if (m_Params.massifChance <= 0.0f)
			{
				return 0.0f;
			}

			const real cellSize = m_Params.massifCellSize;
			const i32 cellX = FastFloor(x / cellSize);
			const i32 cellZ = FastFloor(z / cellSize);
			const u32 h = HashCoords(cellX, cellZ, DeriveSeed(m_Seed, SEED_MASSIF_CELLS));
			if (HashToUnit(h) >= m_Params.massifChance)
			{
				return 0.0f;
			}

			const u32 h2 = HashU32(h ^ 0x68e31da4u);
			const u32 h3 = HashU32(h2 + 0x1b56c4e9u);
			const u32 h4 = HashU32(h3 ^ 0x9e3779b9u);
			const real centerX = ((real)cellX + 0.5f + (HashToUnit(h2) - 0.5f) * 0.36f) * cellSize;
			const real centerZ = ((real)cellZ + 0.5f + (HashToUnit(h3) - 0.5f) * 0.36f) * cellSize;
			// Footprint (including warp) stays within the cell, so only the containing cell needs checking
			const real radius = cellSize * (0.16f + 0.06f * HashToUnit(h4));

			const real dx = x - centerX;
			const real dz = z - centerZ;
			const real maxReach = radius * 1.35f;
			if (dx * dx + dz * dz > maxReach * maxReach)
			{
				return 0.0f;
			}

			// Warp the footprint so it isn't circular
			const real warpInv = 1.0f / (radius * 0.6f);
			const u32 warpSeed = DeriveSeed(m_Seed, SEED_MASSIF_WARP);
			const real wx = Fbm2(x * warpInv, z * warpInv, 3, warpSeed) * radius * 0.3f;
			const real wz = Fbm2(x * warpInv + 5.2f, z * warpInv + 1.3f, 3, warpSeed + 7) * radius * 0.3f;
			const real ux = (dx + wx) / radius;
			const real uz = (dz + wz) / radius;
			const real t2 = ux * ux + uz * uz;
			if (t2 >= 1.0f)
			{
				return 0.0f;
			}

			const real profile = (1.0f - t2) * (1.0f - t2);
			const real peakHeight = Lerp(m_Params.massifMinHeight, m_Params.massifMaxHeight, HashToUnit(HashU32(h4 ^ 0x2545f491u)));

			// Domain warped ridged multifractal gives sharp ridges radiating from high ground, with valleys between them
			const real inv = 1.0f / 3600.0f;
			const real rx = (x + wx * 0.6f) * inv;
			const real rz = (z + wz * 0.6f) * inv;
			const real ridges = Ridged2(rx, rz, m_MassifOctaves, DeriveSeed(m_Seed, SEED_MASSIF_RIDGES));
			const real summit = profile * profile * profile;
			real shape = std::pow(profile, 1.2f) * (0.18f + 0.82f * ridges) + 0.3f * summit * ridges;
			shape /= 1.3f;

			const real detailInv = 1.0f / 600.0f;
			const real detail = ErodedFbm2(x * detailInv, z * detailInv, 6, DeriveSeed(m_Seed, SEED_MASSIF_DETAIL));

			outWeight = Smoothstep(0.0f, 0.3f, profile);
			return peakHeight * shape + 40.0f * detail * profile;
		}

		ColumnSample TerrainDensity::SampleBaseColumn(real x, real z) const
		{
			const real cellSize = m_Params.biomeCellSize;
			const real invCellSize = 1.0f / cellSize;

			// Warp biome cells so their boundaries aren't straight lines
			const real warpScale = 1.0f / (cellSize * 0.7f);
			const real warpX = Fbm2(x * warpScale, z * warpScale, 3, DeriveSeed(m_Seed, SEED_BIOME_WARP_X));
			const real warpZ = Fbm2(x * warpScale, z * warpScale, 3, DeriveSeed(m_Seed, SEED_BIOME_WARP_Z));
			const real qx = (x + warpX * cellSize * 0.4f) * invCellSize;
			const real qz = (z + warpZ * cellSize * 0.4f) * invCellSize;
			const i32 cellX = FastFloor(qx);
			const i32 cellY = FastFloor(qz);

			const u32 cellSeed = DeriveSeed(m_Seed, SEED_BIOME_CELLS);
			const i32 biomeCount = (i32)m_Params.biomes.size();

			real distances[9];
			i32 biomes[9];
			real nearestDist = 1e30f;
			for (i32 j = -1; j <= 1; ++j)
			{
				for (i32 i = -1; i <= 1; ++i)
				{
					const i32 cx = cellX + i;
					const i32 cy = cellY + j;
					const u32 h = HashCoords(cx, cy, cellSeed);
					const real jitterX = (real)(h & 0xFFFF) / 65535.0f;
					const real jitterY = (real)(h >> 16) / 65535.0f;
					const real dx = (real)cx + 0.5f + (jitterX - 0.5f) * 0.85f - qx;
					const real dy = (real)cy + 0.5f + (jitterY - 0.5f) * 0.85f - qz;
					const real dist = std::sqrt(dx * dx + dy * dy);

					// Pick a biome type for this cell, weighted by biome frequencies & how well they suit the local climate
					BiomeCellCacheEntry& cacheEntry = t_BiomeCellCache[HashCoords(cx, cy, m_InstanceID) & (FEATURE_CACHE_SIZE - 1)];
					i32 biome;
					if (cacheEntry.instanceID == m_InstanceID && cacheEntry.cellX == cx && cacheEntry.cellZ == cy)
					{
						biome = cacheEntry.biome;
					}
					else
					{
						const real temperature = Temperature(((real)cx + 0.5f) * cellSize, ((real)cy + 0.5f) * cellSize);
						real biomeWeights[MAX_BIOME_COUNT];
						real totalWeight = 0.0f;
						for (i32 b = 0; b < biomeCount; ++b)
						{
							const real t = (temperature - m_Params.biomes[b].temperature) / BIOME_TEMPERATURE_RANGE;
							// Small floor so every biome can still (rarely) appear anywhere
							biomeWeights[b] = m_Params.biomes[b].frequency * (std::exp(-t * t) + 0.01f);
							totalWeight += biomeWeights[b];
						}

						real r = HashToUnit(HashU32(h ^ 0x5bd1e995u)) * totalWeight;
						biome = biomeCount - 1;
						for (i32 b = 0; b < biomeCount; ++b)
						{
							r -= biomeWeights[b];
							if (r < 0.0f)
							{
								biome = b;
								break;
							}
						}
						cacheEntry = BiomeCellCacheEntry{ m_InstanceID, cx, cy, biome };
					}

					const i32 index = (j + 1) * 3 + (i + 1);
					distances[index] = dist;
					biomes[index] = biome;
					nearestDist = Min(nearestDist, dist);
				}
			}

			// Blend between cells within blend distance of the nearest cell's edge
			real weights[MAX_BIOME_COUNT] = {};
			const real invBlend = cellSize / m_Params.biomeBlendDistance;
			for (i32 i = 0; i < 9; ++i)
			{
				const real t = Clamp((distances[i] - nearestDist) * invBlend, 0.0f, 1.0f);
				const real w = 1.0f - t * t * (3.0f - 2.0f * t);
				weights[biomes[i]] += w * w;
			}

			ColumnSample result = {};
			result.primaryBiome = 0;
			result.secondaryBiome = -1;
			real totalWeight = 0.0f;
			real maxWeight = 0.0f;
			real secondWeight = 0.0f;
			for (i32 b = 0; b < biomeCount; ++b)
			{
				const real w = weights[b];
				if (w <= 1e-4f)
				{
					continue;
				}

				const BiomeParams& biome = m_Params.biomes[b];
				totalWeight += w;
				result.height += w * BiomeHeight((u32)b, x, z);
				result.overhangAmplitude += w * biome.overhangAmplitude;
				result.archStrength += w * biome.archStrength;
				result.caveStrength += w * biome.caveStrength;
				result.lakeStrength += w * biome.lakeStrength;
				result.grassiness += w * biome.grassiness;
				result.temperature += w * biome.temperature;
				result.groundColour.x += w * biome.groundColour.x;
				result.groundColour.y += w * biome.groundColour.y;
				result.groundColour.z += w * biome.groundColour.z;
				result.cliffColour.x += w * biome.cliffColour.x;
				result.cliffColour.y += w * biome.cliffColour.y;
				result.cliffColour.z += w * biome.cliffColour.z;
				result.peakColour.x += w * biome.peakColour.x;
				result.peakColour.y += w * biome.peakColour.y;
				result.peakColour.z += w * biome.peakColour.z;
				result.peakHeight += w * Min(biome.peakHeight, 20000.0f);

				if (w > maxWeight)
				{
					if (maxWeight > 0.0f)
					{
						secondWeight = maxWeight;
						result.secondaryBiome = result.primaryBiome;
					}
					maxWeight = w;
					result.primaryBiome = b;
				}
				else if (w > secondWeight)
				{
					secondWeight = w;
					result.secondaryBiome = b;
				}
			}

			const real invTotalWeight = 1.0f / totalWeight;
			result.height *= invTotalWeight;
			result.overhangAmplitude *= invTotalWeight;
			result.archStrength *= invTotalWeight;
			result.caveStrength *= invTotalWeight;
			result.lakeStrength *= invTotalWeight;
			result.grassiness *= invTotalWeight;
			result.temperature *= invTotalWeight;
			result.groundColour *= invTotalWeight;
			result.cliffColour *= invTotalWeight;
			result.peakColour *= invTotalWeight;
			result.peakHeight *= invTotalWeight;
			result.primaryBiomeWeight = maxWeight * invTotalWeight;
			result.primaryBiomeShare = maxWeight / (maxWeight + secondWeight);

			const real invContinentWavelength = 1.0f / m_Params.continentWavelength;
			result.height += m_Params.continentAmplitude * Fbm2(x * invContinentWavelength, z * invContinentWavelength, 3, DeriveSeed(m_Seed, SEED_CONTINENT));

			real massifWeight;
			const real massifHeight = MassifHeight(x, z, massifWeight);
			if (massifHeight != 0.0f || massifWeight > 0.0f)
			{
				const real w = massifWeight;
				result.height += massifHeight;
				result.massifWeight = w;
				result.overhangAmplitude = Lerp(result.overhangAmplitude, Max(result.overhangAmplitude, MASSIF_OVERHANG), w);
				result.cliffColour = glm::mix(result.cliffColour, glm::vec3(0.42f, 0.41f, 0.43f), w);
				result.groundColour = glm::mix(result.groundColour, glm::vec3(0.30f, 0.37f, 0.21f), w * 0.6f);
				result.peakColour = glm::mix(result.peakColour, glm::vec3(0.93f, 0.95f, 0.98f), w);
				result.peakHeight = Lerp(result.peakHeight, Min(result.peakHeight, 1100.0f), w);
				result.grassiness = Lerp(result.grassiness, Max(result.grassiness, 0.7f), w);
				result.caveStrength *= 1.0f - w * 0.5f;
				result.lakeStrength *= 1.0f - w;
				result.archStrength *= 1.0f - w;
			}

			result.lakeID = 0;
			result.waterLevel = -1e30f;
			result.lakeT = 2.0f;
			result.archRadius = 0.0f;

			if (result.caveStrength > 0.01f)
			{
				result.caveEntrance = Smoothstep(0.35f, 0.6f, GradientNoise2(x * 0.004f, z * 0.004f, DeriveSeed(m_Seed, SEED_CAVE_ENTRANCE)));
			}

			// Caverns are confined to regions so their (expensive) noise only needs evaluating there
			if (m_Params.cavernChance > 0.0f && result.caveStrength > 0.01f)
			{
				const real regionInv = 1.0f / (m_Params.cavernWavelength * 6.0f);
				const real region = GradientNoise2(x * regionInv, z * regionInv, DeriveSeed(m_Seed, SEED_CAVERN) + 7);
				result.cavernStrength = Smoothstep(0.0f, 0.25f, region) * Min(result.caveStrength * 1.5f, 1.0f);
			}

			ApplyOcean(x, z, result);

			result.baseHeight = result.height;

			return result;
		}

		TerrainDensity::LakeInfo TerrainDensity::GetLakeInfo(i32 cellX, i32 cellZ, real centerX, real centerZ, real radius) const
		{
			LakeCacheEntry& entry = t_LakeCache[HashCoords(cellX, cellZ, m_InstanceID) & (FEATURE_CACHE_SIZE - 1)];
			if (entry.instanceID == m_InstanceID && entry.cellX == cellX && entry.cellZ == cellZ)
			{
				return LakeInfo{ entry.bValid, entry.level };
			}

			LakeInfo info = {};
			const ColumnSample center = SampleBaseColumn(centerX, centerZ);
			const u32 h = HashCoords(cellX, cellZ, DeriveSeed(m_Seed, SEED_LAKE_CELLS));
			if (HashToUnit(HashU32(h ^ 0x85ebca6bu)) < center.lakeStrength && center.massifWeight < 0.05f)
			{
				// Sample the surrounding terrain to find a level which the shore contains
				real minRim = 1e30f;
				real maxRim = -1e30f;
				const i32 rimSampleCount = 8;
				for (i32 i = 0; i < rimSampleCount; ++i)
				{
					const real angle = (real)i / (real)rimSampleCount * 6.28318531f;
					const real rimHeight = SampleBaseColumn(centerX + std::cos(angle) * radius * 1.25f, centerZ + std::sin(angle) * radius * 1.25f).height;
					minRim = Min(minRim, rimHeight);
					maxRim = Max(maxRim, rimHeight);
				}

				const real relief = Max(maxRim, center.height) - Min(minRim, center.height);
				info.level = minRim - 2.5f;
				info.bValid = relief < m_Params.lakeMaxRelief && (center.height - info.level) < m_Params.lakeMaxRelief * 0.6f;

				// Lakes (including their surrounding blend region) mustn't overlap the ocean's surface
				for (i32 i = 0; i < rimSampleCount && info.bValid; ++i)
				{
					const real angle = (real)i / (real)rimSampleCount * 6.28318531f;
					info.bValid = OceanWeight(centerX + std::cos(angle) * radius * 1.8f, centerZ + std::sin(angle) * radius * 1.8f) <= 0.0f;
				}
			}

			entry = LakeCacheEntry{ m_InstanceID, cellX, cellZ, info.bValid, info.level };
			return info;
		}

		void TerrainDensity::ApplyLake(real x, real z, ColumnSample& column) const
		{
			if (m_Params.lakeChance <= 0.0f)
			{
				return;
			}

			const real cellSize = m_Params.lakeCellSize;
			const i32 cellX = FastFloor(x / cellSize);
			const i32 cellZ = FastFloor(z / cellSize);
			const u32 h = HashCoords(cellX, cellZ, DeriveSeed(m_Seed, SEED_LAKE_CELLS));
			if (HashToUnit(h) >= m_Params.lakeChance)
			{
				return;
			}

			const u32 h2 = HashU32(h ^ 0x27d4eb2fu);
			const u32 h3 = HashU32(h2 ^ 0x165667b1u);
			const u32 h4 = HashU32(h3 ^ 0xd3a2646cu);
			const real offsetX = (HashToUnit(h2) - 0.5f) * 0.6f;
			const real offsetZ = (HashToUnit(h3) - 0.5f) * 0.6f;
			const real centerX = ((real)cellX + 0.5f + offsetX) * cellSize;
			const real centerZ = ((real)cellZ + 0.5f + offsetZ) * cellSize;
			// Lakes (& their surrounding blend region) stay within their cell, so only the containing cell needs checking
			const real maxReachScale = 1.7f * 1.3f;
			const real maxRadius = (0.5f - Max(Abs(offsetX), Abs(offsetZ))) * cellSize / maxReachScale;
			const real t4 = HashToUnit(h4);
			const real radius = Min(Lerp(m_Params.lakeMinRadius, m_Params.lakeMaxRadius, t4 * t4), maxRadius);
			if (radius < m_Params.lakeMinRadius * 0.5f)
			{
				return;
			}

			const real dx = x - centerX;
			const real dz = z - centerZ;
			const real dist2 = dx * dx + dz * dz;
			const real maxReach = radius * maxReachScale;
			if (dist2 > maxReach * maxReach)
			{
				return;
			}

			// Irregular shoreline
			const u32 shoreSeed = DeriveSeed(m_Seed, SEED_LAKE_SHORE);
			const real shoreInv = 1.0f / (radius * 0.6f);
			const real warp = 0.22f * GradientNoise2(x * shoreInv, z * shoreInv, shoreSeed) +
				0.07f * GradientNoise2(x * shoreInv * 3.1f, z * shoreInv * 3.1f, shoreSeed + 1);
			const real t = std::sqrt(dist2) / (radius * (1.0f + warp));
			if (t >= 1.7f)
			{
				return;
			}

			const LakeInfo info = GetLakeInfo(cellX, cellZ, centerX, centerZ, radius);
			if (!info.bValid)
			{
				return;
			}

			const real level = info.level;
			if (t < 1.0f)
			{
				const real depth = Clamp(radius * 0.05f, 3.0f, 20.0f);
				const real bedNoise = GradientNoise2(x * 0.025f, z * 0.025f, DeriveSeed(m_Seed, SEED_LAKE_BED));
				column.height = level - depth * (1.0f - t * t) + 1.5f * bedNoise * (1.0f - t);
			}
			else
			{
				// Raise a lip around the lake so water is contained, then blend back to the natural terrain
				const real rise = 2.5f * Smoothstep(1.0f, 1.15f, t);
				const real s = Smoothstep(1.15f, 1.7f, t);
				column.height = Lerp(level + rise, column.height, s);
			}

			column.lakeID = h | 1u;
			column.waterLevel = level;
			column.lakeT = t;

			const real keep = Smoothstep(1.0f, 1.7f, t);
			column.caveStrength *= keep;
			column.overhangAmplitude *= keep;
			column.archStrength *= keep;
		}

		TerrainDensity::ArchInfo TerrainDensity::GetArchInfo(i32 cellX, i32 cellZ, real centerX, real centerZ) const
		{
			ArchCacheEntry& entry = t_ArchCache[HashCoords(cellX, cellZ, m_InstanceID) & (FEATURE_CACHE_SIZE - 1)];
			if (entry.instanceID == m_InstanceID && entry.cellX == cellX && entry.cellZ == cellZ)
			{
				return ArchInfo{ entry.bValid, entry.baseHeight };
			}

			ArchInfo info = {};
			const u32 h = HashCoords(cellX, cellZ, DeriveSeed(m_Seed, SEED_ARCH_CELLS));
			ColumnSample center = SampleBaseColumn(centerX, centerZ);
			if (HashToUnit(h) < ARCH_MAX_CHANCE * center.archStrength)
			{
				ApplyLake(centerX, centerZ, center);
				info.bValid = center.lakeID == 0 || center.lakeT > 1.6f;
				info.baseHeight = center.height;
			}

			entry = ArchCacheEntry{ m_InstanceID, cellX, cellZ, info.bValid, info.baseHeight };
			return info;
		}

		void TerrainDensity::ApplyArch(real x, real z, ColumnSample& column) const
		{
			const real cellSize = m_Params.archCellSize;
			const i32 cellX = FastFloor(x / cellSize);
			const i32 cellZ = FastFloor(z / cellSize);
			const u32 h = HashCoords(cellX, cellZ, DeriveSeed(m_Seed, SEED_ARCH_CELLS));
			if (HashToUnit(h) >= ARCH_MAX_CHANCE)
			{
				return;
			}

			const u32 h2 = HashU32(h ^ 0x3c6ef372u);
			const u32 h3 = HashU32(h2 ^ 0xa54ff53au);
			const u32 h4 = HashU32(h3 ^ 0x510e527fu);
			const u32 h5 = HashU32(h4 ^ 0x9b05688cu);
			const real centerX = ((real)cellX + 0.3f + 0.4f * HashToUnit(h2)) * cellSize;
			const real centerZ = ((real)cellZ + 0.3f + 0.4f * HashToUnit(h3)) * cellSize;
			const real radius = Lerp(m_Params.archMinRadius, m_Params.archMaxRadius, HashToUnit(h4));
			const real thickness = radius * (0.16f + 0.08f * HashToUnit(h5));

			const real reach = radius * 1.15f + thickness * 1.5f / 0.55f + 6.0f;
			if (Abs(x - centerX) > reach || Abs(z - centerZ) > reach)
			{
				return;
			}

			const ArchInfo info = GetArchInfo(cellX, cellZ, centerX, centerZ);
			if (!info.bValid)
			{
				return;
			}

			const real angle = HashToUnit(HashU32(h5 ^ 0x1f83d9abu)) * 6.28318531f;
			column.archRadius = radius;
			column.archThickness = thickness;
			// Sunk slightly into the ground so the legs are planted
			column.archCenter = glm::vec3(centerX, info.baseHeight - radius * 0.15f, centerZ);
			column.archDirX = std::cos(angle);
			column.archDirZ = std::sin(angle);
		}

		ColumnSample TerrainDensity::SampleColumn(real x, real z) const
		{
			ColumnSample column = SampleBaseColumn(x, z);
			ApplyLake(x, z, column);
			ApplyArch(x, z, column);
			return column;
		}

		real TerrainDensity::Sample(const glm::vec3& pos, const ColumnSample& column, const EditList& edits /* = {} */) const
		{
			const real px = pos.x;
			const real py = pos.y;
			const real pz = pos.z;
			real d = py - column.height;

			// Overhangs & cliffs: displace the surface with 3D noise, stretched vertically to form ledges
			const real overhangAmplitude = column.overhangAmplitude;
			if (overhangAmplitude > 0.01f)
			{
				const real absD = Abs(d);
				const real cutoff = overhangAmplitude * 1.5f;
				if (absD < cutoff)
				{
					const real fade = 1.0f - Smoothstep(overhangAmplitude, cutoff, absD);
					const real inv = 1.0f / m_Params.overhangWavelength;
					d += overhangAmplitude * fade * 1.6f * Fbm3(px * inv, py * inv * 1.8f, pz * inv, 3, DeriveSeed(m_Seed, SEED_OVERHANG));
				}
			}

			// Arches: a half torus of rock standing on the ground, thicker at its legs
			if (column.archRadius > 0.0f)
			{
				const real radius = column.archRadius;
				const real thickness = column.archThickness;
				const real qx = px - column.archCenter.x;
				const real qy = py - column.archCenter.y;
				const real qz = pz - column.archCenter.z;
				if (qy > -radius * 0.5f - thickness && qy < radius + thickness * 2.0f + 4.0f)
				{
					const real u = qx * column.archDirX + qz * column.archDirZ;
					const real w = -qx * column.archDirZ + qz * column.archDirX;
					const real ring = Length2(u, qy) - radius;
					// A fin of rock: deeper (along w) than it is thick, & thicker towards the legs
					const real legs = 1.0f + 0.5f * (1.0f - Clamp(qy / radius, 0.0f, 1.0f));
					real arch = Length2(ring, w * 0.55f) - thickness * legs;
					// Remove the bottom of the torus where it could poke out below sloped ground
					arch = Max(arch, -(qy + radius * 0.2f));
					const u32 noiseSeed = DeriveSeed(m_Seed, SEED_ARCH_NOISE);
					arch += 2.2f * GradientNoise3(px * 0.08f, py * 0.12f, pz * 0.08f, noiseSeed) +
						0.8f * GradientNoise3(px * 0.3f, py * 0.4f, pz * 0.3f, noiseSeed + 1);
					d = SmoothMin(d, arch, 3.0f);
				}
			}

			// Caves: tunnels (intersections of two noise isosurfaces) & caverns carved out below the surface
			const real caveStrength = column.caveStrength;
			if (caveStrength > 0.01f)
			{
				const real depth = -d;
				const real caveMaxDepth = m_Params.caveMaxDepth;
				if (depth > -8.0f && depth < caveMaxDepth)
				{
					const real bottom = 1.0f - Smoothstep(caveMaxDepth * 0.6f, caveMaxDepth, depth);

					if (m_Params.tunnelRadius > 0.0f)
					{
						const real inv = 1.0f / m_Params.caveWavelength;
						const real qx = px * inv;
						const real qy = py * inv;
						const real qz = pz * inv;
						const real minGradient = 0.25f;

						// Tunnels exist where a mask noise is above a threshold (rising in biomes with fewer caves, to above the
						// noise's range (~[-1, 1]) where caveStrength reaches 0.01, so skipping caves below that is exact).
						// Rather than narrowing as they fade out (leaving long, impassable cracks), tunnels keep their full
						// size until a few meters from where they end, so the distance to the threshold is estimated (in meters).
						real mgx, mgy, mgz;
						const real maskNoise = GradientNoise3D(qx * 0.3f, qy * 0.3f, qz * 0.3f, DeriveSeed(m_Seed, SEED_CAVE_MASK), mgx, mgy, mgz);
						const real maskThreshold = Lerp(1.2f, -0.3f, Smoothstep(0.01f, 0.6f, caveStrength));
						const real maskGradient = Max(std::sqrt(mgx * mgx + mgy * mgy + mgz * mgz), minGradient) * 0.3f * inv;
						const real maskDist = (maskNoise - maskThreshold) / maskGradient;
						// Width of the band over which tunnels close up (meters)
						const real closeDistance = 5.0f;
						if (maskDist > 0.0f)
						{
							// Tunnels follow the curves where two noise isosurfaces intersect. Stretching the noise vertically
							// flattens the isosurfaces, so tunnels run mostly horizontally (gentle, walkable slopes).
							const real verticalStretch = 2.2f;
							real g0x, g0y, g0z, g1x, g1y, g1z;
							const real n0 = GradientNoise3D(qx, qy * verticalStretch, qz, DeriveSeed(m_Seed, SEED_CAVE_0), g0x, g0y, g0z);
							const real n1 = GradientNoise3D(qx - 13.1f, qy * verticalStretch + 47.9f, qz + 23.3f, DeriveSeed(m_Seed, SEED_CAVE_1), g1x, g1y, g1z);

							// The tunnel's axis (where both noises are zero) is estimated with one Newton step, to find how
							// deep it is: tunnels are closed off by depth along their axis, so whole cross sections end
							// together (rather than the ceiling being cut off first, flattening the tunnel into a crack)
							const real w0x = g0x * inv, w0y = g0y * verticalStretch * inv, w0z = g0z * inv;
							const real w1x = g1x * inv, w1y = g1y * verticalStretch * inv, w1z = g1z * inv;
							const real a = w0x * w0x + w0y * w0y + w0z * w0z;
							const real b = w0x * w1x + w0y * w1y + w0z * w1z;
							const real c = w1x * w1x + w1y * w1y + w1z * w1z;
							const real det = Max(a * c - b * b, 0.05f * a * c + 1e-12f);
							const real l0 = (c * n0 - b * n1) / det;
							const real l1 = (a * n1 - b * n0) / det;
							const real maxOffset = m_Params.tunnelRadius * 2.0f;
							const real axisOffsetY = Clamp(l0 * w0y + l1 * w1y, -maxOffset, maxOffset); // Of the point above the axis
							const real axisDepth = depth + axisOffsetY;

							// Tunnels stay well below the surface except at occasional entrances (otherwise tunnels running
							// just below the surface open up into long trenches), & above the maximum depth
							const real topStart = Lerp(m_Params.tunnelRadius * 2.0f + 6.0f, -8.0f, column.caveEntrance);
							const real topDist = (axisDepth - topStart) * 2.0f;
							const real bottomDist = (caveMaxDepth * 0.8f - axisDepth) * 2.0f;
							const real openness = Smoothstep(0.0f, closeDistance, Min(maskDist, Min(topDist, bottomDist)));
							if (openness > 0.0f)
							{
								// Width varies slowly along tunnels, but never narrows enough to become impassable
								const real sizeVariation = Max(0.95f + 0.55f * GradientNoise3(px * 0.006f, py * 0.006f, pz * 0.006f, DeriveSeed(m_Seed, SEED_TUNNEL_SIZE)), 0.6f);
								// Fully closed (by a margin larger than SmoothMax's, so skipping closed tunnels never changes
								// the surface) where tunnels end
								const real radius = Lerp(-8.0f, m_Params.tunnelRadius * sizeVariation, openness);

								// Distance (in meters) to each isosurface, estimated by dividing by the gradient length.
								// Vertical distances are weighted more heavily so tunnels are a little flatter than they are
								// wide. Without this normalization, tunnel width would vary with the noise gradient (pinching
								// shut in places).
								const real flatten = 1.2f;
								const real sy = verticalStretch / flatten;
								const real len0 = Max(std::sqrt(g0x * g0x + g0y * g0y * sy * sy + g0z * g0z), minGradient) * inv;
								const real len1 = Max(std::sqrt(g1x * g1x + g1y * g1y * sy * sy + g1z * g1z), minGradient) * inv;
								const real tube = Length2(n0 / len0, n1 / len1) - radius;
								d = SmoothMax(d, -tube, 4.0f);
							}
						}
					}

					// Caverns: occasional large, flattened chambers deeper down
					if (column.cavernStrength > 0.0f && depth > 18.0f)
					{
						const real inv = 1.0f / m_Params.cavernWavelength;
						const u32 cavernSeed = DeriveSeed(m_Seed, SEED_CAVERN);
						const real c = GradientNoise3(px * inv, py * inv * 2.2f, pz * inv, cavernSeed) +
							0.3f * GradientNoise3(px * inv * 3.1f, py * inv * 5.0f, pz * inv * 3.1f, cavernSeed + 1);
						// Shrink caverns near the surface, the maximum depth & the edges of cavern regions (the threshold
						// rises above the noise's range where they're not allowed, so skipping them elsewhere only changes
						// density values deep inside rock, never the surface)
						const real allowed = Smoothstep(18.0f, 45.0f, depth) * bottom * column.cavernStrength;
						const real threshold = Lerp(0.75f, 0.42f, m_Params.cavernChance) + (1.0f - allowed) * 1.2f;
						const real cavern = (threshold - c) * m_Params.cavernWavelength * 0.3f;
						d = SmoothMax(d, -cavern, 6.0f);
					}
				}
			}

			// Player edits, applied in order. Edits only affect points within a band around them so chunks which
			// gather slightly different sets of nearby edits still agree on shared samples.
			const real band = EditBand();
			for (u32 i = 0; i < edits.count; ++i)
			{
				const Edit& edit = edits.edits[i];
				if (IsPlaneEdit(edit.type))
				{
					// Blends towards the plane's (signed distance) density, so there's no band: the weight is 0 at the
					// edge of the edit's bounds
					const glm::vec3 offset(px - edit.center.x, py - edit.center.y, pz - edit.center.z);
					const real w = PlaneEditWeight(edit, offset);
					if (w <= 0.0f)
					{
						continue;
					}
					const real plane = offset.x * edit.normal.x + offset.y * edit.normal.y + offset.z * edit.normal.z;
					real target = plane;
					if (edit.type == EditType::LOWER)
					{
						target = Max(d, plane);
					}
					else if (edit.type == EditType::RAISE)
					{
						target = Min(d, plane);
					}
					d += (target - d) * w;
					continue;
				}
				const real dist = Length3(px - edit.center.x, py - edit.center.y, pz - edit.center.z);
				if (dist >= edit.radius + band || edit.type == EditType::REMOVE_PLANT)
				{
					continue;
				}
				if (edit.type == EditType::DIG)
				{
					d = Max(d, edit.radius - dist);
				}
				else
				{
					d = Min(d, dist - edit.radius);
				}
			}

			return d;
		}

		real TerrainDensity::Sample(const glm::vec3& pos, const EditList& edits /* = {} */) const
		{
			return Sample(pos, SampleColumn(pos.x, pos.z), edits);
		}

		static SurfaceAttributes MaterialSurface(Material material, const ColumnSample& column)
		{
			const MaterialInfo& info = s_Materials[(u32)material];
			SurfaceAttributes result;
			glm::vec3 colour = info.colour;
			if (material == Material::GRASS)
			{
				colour = column.groundColour;
			}
			else if (material == Material::ROCK)
			{
				colour = column.cliffColour;
			}
			result.colour = glm::vec4(colour, info.vein);
			result.weights = info.weights;
			return result;
		}

		static SurfaceAttributes MixSurface(const SurfaceAttributes& a, const SurfaceAttributes& b, real t)
		{
			SurfaceAttributes result;
			result.colour = a.colour + (b.colour - a.colour) * t;
			result.weights = a.weights + (b.weights - a.weights) * t;
			return result;
		}

		SurfaceAttributes TerrainDensity::ComputeSurface(const glm::vec3& pos, const glm::vec3& normal, const ColumnSample& column, const EditList& edits) const
		{
			// The latest placed material containing this point wins. Dug surfaces expose whatever was there before.
			bool bDug = false;
			for (i32 i = (i32)edits.count - 1; i >= 0; --i)
			{
				const Edit& edit = edits.edits[i];
				if (IsPlaneEdit(edit.type))
				{
					if (PlaneEditWeight(edit, pos - edit.center) <= 0.0f)
					{
						continue;
					}
					// Surfaces which were in the air before this edit are its placed material, the rest were cut into
					EditList before;
					before.edits = edits.edits;
					before.count = (u32)i;
					const bool bWasAir = edit.type != EditType::LOWER && Sample(pos, column, before) > 0.05f;
					if (bWasAir)
					{
						SurfaceAttributes placed = MaterialSurface(edit.material, column);
						const real variation = GradientNoise3(pos.x * 0.3f, pos.y * 0.3f, pos.z * 0.3f, DeriveSeed(m_Seed, SEED_COLOUR) + 1);
						placed.colour.x *= 1.0f + variation * 0.06f;
						placed.colour.y *= 1.0f + variation * 0.06f;
						placed.colour.z *= 1.0f + variation * 0.06f;
						return placed;
					}
					bDug = bDug || edit.type != EditType::RAISE;
					continue;
				}
				const real dist = glm::distance(pos, edit.center);
				const real tolerance = edit.radius * 0.05f + m_Params.voxelSize * 0.5f;
				if (dist > edit.radius + tolerance || edit.type == EditType::REMOVE_PLANT)
				{
					continue;
				}
				if (edit.type == EditType::FILL)
				{
					SurfaceAttributes placed = MaterialSurface(edit.material, column);
					const real variation = GradientNoise3(pos.x * 0.3f, pos.y * 0.3f, pos.z * 0.3f, DeriveSeed(m_Seed, SEED_COLOUR) + 1);
					placed.colour.x *= 1.0f + variation * 0.06f;
					placed.colour.y *= 1.0f + variation * 0.06f;
					placed.colour.z *= 1.0f + variation * 0.06f;
					return placed;
				}
				bDug = true;
			}

			const u32 seed = DeriveSeed(m_Seed, SEED_COLOUR);
			const real variation = GradientNoise3(pos.x * 0.03f, pos.y * 0.03f, pos.z * 0.03f, seed);

			const real flatness = Smoothstep(0.55f, 0.8f, normal.y + variation * 0.08f);

			SurfaceAttributes ground;
			const real grassiness = column.grassiness * Smoothstep(0.6f, 0.85f, normal.y);
			ground.colour = glm::vec4(column.groundColour, 0.0f);
			ground.weights = glm::vec4(grassiness, 0.0f, 1.0f - grassiness, 0.0f);

			SurfaceAttributes cliff;
			cliff.colour = glm::vec4(column.cliffColour * (1.0f + variation * 0.12f), 0.0f);
			cliff.weights = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);

			SurfaceAttributes surface = MixSurface(cliff, ground, flatness);

			// Beaches & lake beds
			if (column.lakeID != 0)
			{
				const real above = pos.y - column.waterLevel;
				// Sea shores get wider beaches than lakes
				const real beachHeight = column.lakeID == OCEAN_ID ? 7.0f : 3.0f;
				const real beach = (1.0f - Smoothstep(beachHeight * 0.4f, beachHeight, above + variation * 0.8f)) * Smoothstep(0.4f, 0.7f, normal.y);
				if (beach > 0.0f)
				{
					SurfaceAttributes sand = MaterialSurface(Material::SAND, column);
					const real wet = 1.0f - Smoothstep(-0.5f, 0.6f, above);
					sand.colour = glm::mix(sand.colour, glm::vec4(0.40f, 0.36f, 0.28f, 0.0f), wet * 0.7f);
					surface = MixSurface(surface, sand, beach);
				}
			}

			// Snow on flat-ish high ground
			const real snow = Smoothstep(column.peakHeight - 60.0f, column.peakHeight + 60.0f, pos.y + variation * 80.0f) * Smoothstep(0.35f, 0.65f, normal.y);
			if (snow > 0.0f)
			{
				SurfaceAttributes snowSurface;
				snowSurface.colour = glm::vec4(column.peakColour, 0.0f);
				snowSurface.weights = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
				surface = MixSurface(surface, snowSurface, snow);
			}

			// Below the original surface (dug out, or caves): soil, then rock
			const real depth = column.height - pos.y;
			const real overhangOffset = bDug ? 0.0f : column.overhangAmplitude * 1.5f;
			const real subsurface = Smoothstep(0.3f + overhangOffset, 1.2f + overhangOffset, depth);
			if (subsurface > 0.0f)
			{
				const real rockiness = Smoothstep(2.0f, 5.0f, depth - overhangOffset + variation * 2.0f);
				SurfaceAttributes under = MixSurface(MaterialSurface(Material::SOIL, column), cliff, rockiness);
				// Darken deep caves
				const real darkening = Lerp(1.0f, 0.45f, Smoothstep(8.0f, 40.0f, depth));
				under.colour.x *= darkening;
				under.colour.y *= darkening;
				under.colour.z *= darkening;
				surface = MixSurface(surface, under, subsurface);
			}

			surface.colour = glm::clamp(surface.colour, glm::vec4(0.0f), glm::vec4(1.0f));
			return surface;
		}


		// -----------------------------------------------------------------------------------------
		// Column grid cache
		// -----------------------------------------------------------------------------------------

		std::shared_ptr<const std::vector<ColumnSample>> ColumnGridCache::Find(i32 level, i32 x, i32 z)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			auto iter = m_Grids.find(ChunkKey{ level, glm::ivec3(x, 0, z) });
			return iter != m_Grids.end() ? iter->second : nullptr;
		}

		void ColumnGridCache::Insert(i32 level, i32 x, i32 z, const std::shared_ptr<const std::vector<ColumnSample>>& grid)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			const ChunkKey key = { level, glm::ivec3(x, 0, z) };
			if (!m_Grids.emplace(key, grid).second)
			{
				return;
			}
			m_Order.push_back(key);
			while (m_Order.size() > CAPACITY)
			{
				m_Grids.erase(m_Order.front());
				m_Order.pop_front();
			}
		}

		// -----------------------------------------------------------------------------------------
		// Chunk keys
		// -----------------------------------------------------------------------------------------

		static inline i32 FloorDiv2(i32 v)
		{
			return v >= 0 ? v / 2 : -((-v + 1) / 2);
		}

		ChunkKey ChunkKey::Parent() const
		{
			return ChunkKey{ level + 1, glm::ivec3(FloorDiv2(index.x), FloorDiv2(index.y), FloorDiv2(index.z)) };
		}

		size_t ChunkKeyHash::operator()(const ChunkKey& key) const
		{
			const u32 h = HashCoords(key.index.x, key.index.y, key.index.z, (u32)key.level);
			return (size_t)h;
		}

		// -----------------------------------------------------------------------------------------
		// Meshing
		// -----------------------------------------------------------------------------------------

		// Minimal open addressing hash map (linear probing), much faster than std::unordered_map in unoptimized builds
		template<typename K, typename V>
		class FlatHashMap
		{
		public:
			FlatHashMap() = default;
			FlatHashMap(const FlatHashMap&) = delete;
			FlatHashMap& operator=(const FlatHashMap&) = delete;

			~FlatHashMap()
			{
				Free();
			}

			// Returns null if not present
			V* Find(K key) const
			{
				if (m_Capacity == 0)
				{
					return nullptr;
				}
				u32 slot = Hash(key) & (m_Capacity - 1);
				while (m_bUsed[slot])
				{
					if (m_Keys[slot] == key)
					{
						return &m_Values[slot];
					}
					slot = (slot + 1) & (m_Capacity - 1);
				}
				return nullptr;
			}

			// Key must not already be present
			V& Insert(K key, const V& value)
			{
				if ((m_Count + 1) * 2 > m_Capacity)
				{
					Grow();
				}
				u32 slot = Hash(key) & (m_Capacity - 1);
				while (m_bUsed[slot])
				{
					slot = (slot + 1) & (m_Capacity - 1);
				}
				m_bUsed[slot] = true;
				m_Keys[slot] = key;
				m_Values[slot] = value;
				++m_Count;
				return m_Values[slot];
			}

		private:
			static u32 Hash(u32 key) { return HashU32(key); }
			static u32 Hash(u64 key) { return HashU32((u32)key ^ HashU32((u32)(key >> 32))); }

			void Grow()
			{
				K* oldKeys = m_Keys;
				V* oldValues = m_Values;
				bool* oldUsed = m_bUsed;
				const u32 oldCapacity = m_Capacity;

				m_Capacity = oldCapacity == 0 ? 1024 : oldCapacity * 2;
				m_Keys = new K[m_Capacity];
				m_Values = new V[m_Capacity];
				m_bUsed = new bool[m_Capacity];
				memset(m_bUsed, 0, m_Capacity * sizeof(bool));
				m_Count = 0;

				for (u32 i = 0; i < oldCapacity; ++i)
				{
					if (oldUsed[i])
					{
						Insert(oldKeys[i], oldValues[i]);
					}
				}

				delete[] oldKeys;
				delete[] oldValues;
				delete[] oldUsed;
			}

			void Free()
			{
				delete[] m_Keys;
				delete[] m_Values;
				delete[] m_bUsed;
				m_Keys = nullptr;
				m_Values = nullptr;
				m_bUsed = nullptr;
				m_Capacity = 0;
				m_Count = 0;
			}

			K* m_Keys = nullptr;
			V* m_Values = nullptr;
			bool* m_bUsed = nullptr;
			u32 m_Capacity = 0;
			u32 m_Count = 0;
		};

		// Generates the mesh of a single chunk using marching tetrahedra.
		//
		// Points are addressed in "half cell" units (a ∈ [-2, 2N + 2]^3), where even coordinates lie on the chunk's
		// sample grid and odd coordinates lie on the grid of a chunk one level finer. Regular cells are split into
		// six tetrahedra around their main diagonal (which induces a (lo,lo)-(hi,hi) diagonal on every face, matching
		// adjacent cells/chunks). Cells touching a face or edge shared with a finer chunk ("transition cells") have
		// those faces/edges subdivided to match the finer chunk, and are split into tetrahedra fanning out from the
		// cell center.
		bool TerrainDensity::FindTopSurface(real x, real z, real yMin, real yMax, const ColumnSample& column, const EditList& edits, real& outY) const
		{
			// Coarse steps down from the top, then bisection
			const real step = m_Params.voxelSize;
			real prevY = yMax;
			real prevD = Sample(glm::vec3(x, prevY, z), column, edits);
			if (prevD < 0.0f)
			{
				// Solid at the top of the range, so the surface (if any) is above it
				return false;
			}
			for (real y = yMax - step; y >= yMin - step; y -= step)
			{
				const real d = Sample(glm::vec3(x, y, z), column, edits);
				if (d < 0.0f)
				{
					real lo = y;
					real hi = prevY;
					for (u32 i = 0; i < 8; ++i)
					{
						const real mid = (lo + hi) * 0.5f;
						if (Sample(glm::vec3(x, mid, z), column, edits) < 0.0f)
						{
							lo = mid;
						}
						else
						{
							hi = mid;
						}
					}
					outY = (lo + hi) * 0.5f;
					return outY >= yMin && outY < yMax;
				}
				prevY = y;
				prevD = d;
			}
			return false;
		}

		// Trees, cacti & bushes, generated with the terrain (so they stream, LOD & collide along with it). Placed on
		// jittered grids, each candidate is decided by its biome's vegetation type, a forest density pattern & the
		// ground's slope. Each biome has one dominant tree species (& a little variety), so forests look uniform.
		static constexpr real PLANT_CELL_SIZE = 7.0f;
		static constexpr real BUSH_CELL_SIZE = 4.5f;
		// Bushes are only shown on the most detailed chunks
		static constexpr i32 MAX_BUSH_LEVEL = 1;

		enum class BushKind : u8
		{
			BERRY, // Dark, rounded, with berries
			LEAFY, // Light green clumps
			LOW_SHRUB, // Low, wide mats (heather, dwarf birch, juniper)
			SCRUB, // Dry, grey-olive tufts
		};

		struct VegetationInfo
		{
			real treeChance; // Chance of a tree per cell in clearings
			real forestTreeChance; // Extra chance of a tree per cell in forests
			// > 0: denser on grassy ground, < 0: sparser on grassy ground (deserts), 0: independent of grass
			real grassResponse;
			PlantSpecies majorSpecies;
			PlantSpecies minorSpecies;
			real minorFraction; // Of trees (they grow in groves)
			real treeScale;
			real bushChance; // Per bush cell
			BushKind bushKind;
			glm::vec3 bushColour;
			glm::vec3 berryColour;
			real berryFraction; // Of bushes with berries
		};

		static const VegetationInfo s_VegetationInfos[] =
		{
			// NONE
			{ 0.0f, 0.0f, 0.0f, PlantSpecies::NONE, PlantSpecies::NONE, 0.0f, 1.0f, 0.0f, BushKind::LEAFY, glm::vec3(0.0f), glm::vec3(0.0f), 0.0f },
			// TAIGA
			{ 0.07f, 0.48f, 1.0f, PlantSpecies::SPRUCE, PlantSpecies::BIRCH, 0.05f, 1.0f, 0.12f, BushKind::BERRY, glm::vec3(0.09f, 0.17f, 0.08f), glm::vec3(0.16f, 0.18f, 0.42f), 0.7f },
			// NORDIC
			{ 0.05f, 0.40f, 1.0f, PlantSpecies::PINE, PlantSpecies::BIRCH, 0.4f, 1.0f, 0.12f, BushKind::BERRY, glm::vec3(0.13f, 0.24f, 0.09f), glm::vec3(0.62f, 0.08f, 0.07f), 0.6f },
			// BROADLEAF
			{ 0.03f, 0.34f, 1.0f, PlantSpecies::OAK, PlantSpecies::BIRCH, 0.08f, 1.0f, 0.10f, BushKind::LEAFY, glm::vec3(0.22f, 0.36f, 0.11f), glm::vec3(0.6f, 0.07f, 0.08f), 0.15f },
			// SWAMP
			{ 0.03f, 0.26f, 1.0f, PlantSpecies::WILLOW, PlantSpecies::BIRCH, 0.2f, 1.0f, 0.16f, BushKind::LEAFY, glm::vec3(0.14f, 0.24f, 0.10f), glm::vec3(0.0f), 0.0f },
			// TUNDRA
			{ 0.025f, 0.0f, 1.0f, PlantSpecies::SPRUCE, PlantSpecies::NONE, 0.0f, 0.45f, 0.22f, BushKind::LOW_SHRUB, glm::vec3(0.30f, 0.27f, 0.14f), glm::vec3(0.55f, 0.1f, 0.08f), 0.25f },
			// ALPINE
			{ 0.02f, 0.14f, 1.0f, PlantSpecies::PINE, PlantSpecies::SPRUCE, 0.2f, 0.85f, 0.10f, BushKind::LOW_SHRUB, glm::vec3(0.13f, 0.20f, 0.11f), glm::vec3(0.0f), 0.0f },
			// SAVANNA
			{ 0.012f, 0.025f, 0.0f, PlantSpecies::ACACIA, PlantSpecies::NONE, 0.0f, 1.0f, 0.08f, BushKind::SCRUB, glm::vec3(0.36f, 0.35f, 0.21f), glm::vec3(0.0f), 0.0f },
			// DESERT
			{ 0.04f, 0.0f, -2.0f, PlantSpecies::SAGUARO, PlantSpecies::NONE, 0.0f, 1.0f, 0.05f, BushKind::SCRUB, glm::vec3(0.38f, 0.36f, 0.24f), glm::vec3(0.0f), 0.0f },
			// VOLCANIC (charred remains of a burnt forest, more of them where the forest was)
			{ 0.006f, 0.03f, 0.0f, PlantSpecies::CHARRED, PlantSpecies::NONE, 0.0f, 1.0f, 0.03f, BushKind::SCRUB, glm::vec3(0.17f, 0.16f, 0.13f), glm::vec3(0.0f), 0.0f },
		};
		static_assert(ARRAY_LENGTH(s_VegetationInfos) == (u32)Vegetation::_COUNT, "s_VegetationInfos must match Vegetation enum");

		static PlantKind SpeciesKind(PlantSpecies species)
		{
			switch (species)
			{
			case PlantSpecies::SPRUCE:
			case PlantSpecies::PINE:
				return PlantKind::CONIFER;
			case PlantSpecies::BIRCH:
			case PlantSpecies::OAK:
			case PlantSpecies::WILLOW:
			case PlantSpecies::ACACIA:
				return PlantKind::DECIDUOUS;
			case PlantSpecies::SAGUARO:
				return PlantKind::CACTUS;
			case PlantSpecies::CHARRED:
				return PlantKind::SNAG;
			default:
				return PlantKind::NONE;
			}
		}

		// The biome whose vegetation grows here: near borders plants are dithered between the two biomes
		static const BiomeParams& VegetationBiome(const WorldParams& params, const ColumnSample& column, real random01)
		{
			const i32 biomeCount = (i32)params.biomes.size();
			const i32 primary = glm::clamp(column.primaryBiome, 0, biomeCount - 1);
			if (column.secondaryBiome >= 0 && column.secondaryBiome < biomeCount)
			{
				// Share is 0.5 right on the border, so plants are evenly mixed there, & only the primary's further in
				const real primaryChance = 0.5f + 0.5f * Smoothstep(0.5f, 0.75f, column.primaryBiomeShare);
				if (random01 >= primaryChance)
				{
					return params.biomes[column.secondaryBiome];
				}
			}
			return params.biomes[primary];
		}

		// [0, 1]: forests grow in patches, with clearings between them
		static real ForestAmount(real x, real z, u32 forestSeed)
		{
			const real forestNoise = GradientNoise3(x / 700.0f, 0.0f, z / 700.0f, forestSeed) + 0.4f * GradientNoise3(x / 180.0f, 0.5f, z / 180.0f, forestSeed + 1);
			return Smoothstep(-0.25f, 0.45f, forestNoise);
		}

		static real GrassFactor(real grassResponse, real grassiness)
		{
			if (grassResponse > 0.0f)
			{
				return grassiness;
			}
			if (grassResponse < 0.0f)
			{
				return Max(1.0f + grassResponse * grassiness, 0.0f);
			}
			return 1.0f;
		}

		static PlantSpecies ChoosePlantSpecies(const WorldParams& params, const ColumnSample& column, real x, real z, u32 hash, u32 forestSeed, real& outScale)
		{
			const BiomeParams& biome = VegetationBiome(params, column, HashToUnit(HashU32(hash + 31)));
			const VegetationInfo& info = s_VegetationInfos[(u32)biome.vegetation];
			outScale = info.treeScale;
			if (info.majorSpecies == PlantSpecies::NONE)
			{
				return PlantSpecies::NONE;
			}

			const real chance = HashToUnit(hash);
			real density = info.treeChance;
			if (info.forestTreeChance > 0.0f)
			{
				density += info.forestTreeChance * ForestAmount(x, z, forestSeed);
			}
			density *= biome.treeDensity * GrassFactor(info.grassResponse, column.grassiness);
			if (chance >= density)
			{
				return PlantSpecies::NONE;
			}

			const real speciesRandom = HashToUnit(HashU32(hash + 77));
			PlantSpecies species = info.majorSpecies;
			if (info.minorFraction > 0.0f)
			{
				// The minor species grows in groves
				const real grove = GradientNoise3(x / 110.0f, 2.5f, z / 110.0f, forestSeed + 2);
				if (speciesRandom < info.minorFraction * Clamp(1.0f + 2.5f * grove, 0.0f, 3.0f))
				{
					species = info.minorSpecies;
				}
			}

			// Towards the tree line only conifers grow
			if (SpeciesKind(species) == PlantKind::DECIDUOUS)
			{
				const real alpine = Smoothstep(column.peakHeight - 600.0f, column.peakHeight - 250.0f, column.height);
				if (HashToUnit(HashU32(hash + 78)) < alpine)
				{
					species = PlantSpecies::SPRUCE;
				}
			}
			return species;
		}

		// Sizes of a plant's parts (VegetationBuilder::AddPlant builds from these)
		static void ComputePlantDimensions(PlantInfo& plant)
		{
			const real s = plant.size01;
			const real scale = plant.scale;
			const real radiusScale = std::sqrt(scale);
			switch (plant.species)
			{
			case PlantSpecies::SPRUCE:
				plant.height = (9.0f + 12.0f * s) * scale;
				plant.trunkHeight = plant.height * 0.35f - 0.3f;
				plant.trunkRadius = (0.22f + 0.15f * s) * radiusScale;
				break;
			case PlantSpecies::PINE:
				plant.height = (10.0f + 10.0f * s) * scale;
				plant.trunkHeight = plant.height * 0.6f - 0.3f;
				plant.trunkRadius = (0.2f + 0.12f * s) * radiusScale;
				break;
			case PlantSpecies::BIRCH:
				plant.height = (8.0f + 6.0f * s) * scale;
				plant.trunkHeight = plant.height * 0.6f - 0.3f;
				plant.trunkRadius = (0.15f + 0.08f * s) * radiusScale;
				break;
			case PlantSpecies::OAK:
				plant.height = (7.0f + 7.0f * s) * scale;
				plant.trunkHeight = plant.height * 0.55f - 0.3f;
				plant.trunkRadius = (0.3f + 0.15f * s) * radiusScale;
				break;
			case PlantSpecies::WILLOW:
				plant.height = (7.0f + 5.0f * s) * scale;
				plant.trunkHeight = plant.height * 0.45f - 0.3f;
				plant.trunkRadius = (0.35f + 0.15f * s) * radiusScale;
				break;
			case PlantSpecies::ACACIA:
				plant.height = (5.0f + 4.0f * s) * scale;
				plant.trunkHeight = plant.height * 0.72f - 0.3f;
				plant.trunkRadius = (0.18f + 0.08f * s) * radiusScale;
				break;
			case PlantSpecies::SAGUARO:
				plant.height = (3.0f + 3.5f * s) * scale;
				plant.trunkHeight = plant.height;
				plant.trunkRadius = (0.28f + 0.1f * s) * radiusScale;
				break;
			case PlantSpecies::CHARRED:
				// The smallest third are stumps, the rest snags whose tops have burnt & broken off
				plant.height = (s < 0.33f ? 0.8f + 1.8f * s : 3.5f + 6.0f * s) * scale;
				plant.trunkHeight = plant.height - 0.3f;
				plant.trunkRadius = (0.2f + 0.14f * s) * radiusScale;
				break;
			default:
				break;
			}
		}

		static bool IsPlantRemoved(const EditList& edits, real x, real z)
		{
			for (u32 i = 0; i < edits.count; ++i)
			{
				const Edit& edit = edits.edits[i];
				if (edit.type == EditType::REMOVE_PLANT && Abs(edit.center.x - x) < 0.05f && Abs(edit.center.z - z) < 0.05f)
				{
					return true;
				}
			}
			return false;
		}

		// Whether a plant can grow at (x, z) within the box's height range, & if so where the ground is
		static bool FindPlantGround(const TerrainDensity& density, const EditList& edits, const ColumnSample& column, real x, real z,
			const glm::vec3& boxMin, const glm::vec3& boxMax, glm::vec3& outBase)
		{
			real groundY;
			if (!density.FindTopSurface(x, z, boxMin.y, boxMax.y, column, edits, groundY))
			{
				return false;
			}
			const glm::vec3 base(x, groundY, z);
			if (column.lakeID != 0 && column.lakeT < 1.05f && groundY < column.waterLevel + 0.5f)
			{
				return false;
			}
			if (groundY > column.peakHeight - 30.0f || SkyVisibility(base, column) < 0.95f)
			{
				return false;
			}
			// Not on steep slopes
			const real e = 1.0f;
			const glm::vec3 gradient(
				density.Sample(base + glm::vec3(e, 0, 0), column, edits) - density.Sample(base - glm::vec3(e, 0, 0), column, edits),
				density.Sample(base + glm::vec3(0, e, 0), column, edits) - density.Sample(base - glm::vec3(0, e, 0), column, edits),
				density.Sample(base + glm::vec3(0, 0, e), column, edits) - density.Sample(base - glm::vec3(0, 0, e), column, edits));
			const real gradientLength = glm::length(gradient);
			if (gradientLength < 1e-5f || gradient.y / gradientLength < 0.8f)
			{
				return false;
			}
			outBase = base;
			return true;
		}

		// Calls func(const PlantInfo&) for each plant whose base lies within the given box. Levels >= 2 skip small plants.
		template<typename Func>
		static void ForEachPlant(const TerrainDensity& density, const EditList& edits, const glm::vec3& boxMin, const glm::vec3& boxMax, i32 level, Func func)
		{
			const u32 seed = DeriveSeed(density.Seed(), SEED_TREES);
			const u32 forestSeed = DeriveSeed(density.Seed(), SEED_FOREST);
			const i32 minCellX = (i32)glm::floor(boxMin.x / PLANT_CELL_SIZE);
			const i32 maxCellX = (i32)glm::floor((boxMax.x - 1e-3f) / PLANT_CELL_SIZE);
			const i32 minCellZ = (i32)glm::floor(boxMin.z / PLANT_CELL_SIZE);
			const i32 maxCellZ = (i32)glm::floor((boxMax.z - 1e-3f) / PLANT_CELL_SIZE);
			for (i32 cz = minCellZ; cz <= maxCellZ; ++cz)
			{
				for (i32 cx = minCellX; cx <= maxCellX; ++cx)
				{
					const u32 h = HashCoords(cx, cz, seed);
					// Distant chunks only show the larger plants
					const real size01 = HashToUnit(HashU32(h + 5));
					if (level >= 2 && size01 < 0.4f)
					{
						continue;
					}

					const real x = ((real)cx + 0.1f + 0.8f * HashToUnit(HashU32(h + 1))) * PLANT_CELL_SIZE;
					const real z = ((real)cz + 0.1f + 0.8f * HashToUnit(HashU32(h + 2))) * PLANT_CELL_SIZE;
					if (x < boxMin.x || x >= boxMax.x || z < boxMin.z || z >= boxMax.z)
					{
						continue;
					}

					const ColumnSample column = density.SampleColumn(x, z);
					real scale;
					const PlantSpecies species = ChoosePlantSpecies(density.Params(), column, x, z, h, forestSeed, scale);
					if (species == PlantSpecies::NONE || IsPlantRemoved(edits, x, z))
					{
						continue;
					}

					glm::vec3 base;
					if (!FindPlantGround(density, edits, column, x, z, boxMin, boxMax, base))
					{
						continue;
					}

					PlantInfo plant;
					plant.kind = SpeciesKind(species);
					plant.species = species;
					plant.base = base;
					plant.hash = h;
					plant.size01 = size01;
					plant.scale = scale;
					ComputePlantDimensions(plant);
					func(plant);
				}
			}
		}

		struct BushInfo
		{
			glm::vec3 base;
			u32 hash;
			const VegetationInfo* vegetation;
		};

		// Bushes aren't collided with or interacted with, so they're only found when building chunks
		template<typename Func>
		static void ForEachBush(const TerrainDensity& density, const EditList& edits, const glm::vec3& boxMin, const glm::vec3& boxMax, Func func)
		{
			// No vegetation type has a higher chance than this (so most cells can be skipped before sampling)
			static const real MAX_BUSH_CHANCE = 0.45f;
			const u32 seed = DeriveSeed(density.Seed(), SEED_BUSHES);
			const u32 forestSeed = DeriveSeed(density.Seed(), SEED_FOREST);
			const WorldParams& params = density.Params();
			const i32 minCellX = (i32)glm::floor(boxMin.x / BUSH_CELL_SIZE);
			const i32 maxCellX = (i32)glm::floor((boxMax.x - 1e-3f) / BUSH_CELL_SIZE);
			const i32 minCellZ = (i32)glm::floor(boxMin.z / BUSH_CELL_SIZE);
			const i32 maxCellZ = (i32)glm::floor((boxMax.z - 1e-3f) / BUSH_CELL_SIZE);
			for (i32 cz = minCellZ; cz <= maxCellZ; ++cz)
			{
				for (i32 cx = minCellX; cx <= maxCellX; ++cx)
				{
					const u32 h = HashCoords(cx, cz, seed);
					const real chance = HashToUnit(h);
					if (chance >= MAX_BUSH_CHANCE)
					{
						continue;
					}

					const real x = ((real)cx + 0.1f + 0.8f * HashToUnit(HashU32(h + 1))) * BUSH_CELL_SIZE;
					const real z = ((real)cz + 0.1f + 0.8f * HashToUnit(HashU32(h + 2))) * BUSH_CELL_SIZE;
					if (x < boxMin.x || x >= boxMax.x || z < boxMin.z || z >= boxMax.z)
					{
						continue;
					}

					const ColumnSample column = density.SampleColumn(x, z);
					const BiomeParams& biome = VegetationBiome(params, column, HashToUnit(HashU32(h + 31)));
					const VegetationInfo& info = s_VegetationInfos[(u32)biome.vegetation];
					// More undergrowth in & around forests
					const real forest = info.forestTreeChance > 0.0f ? ForestAmount(x, z, forestSeed) : 0.5f;
					const real bushChance = Min(info.bushChance * biome.bushDensity * (0.5f + forest) * GrassFactor(info.grassResponse, column.grassiness), MAX_BUSH_CHANCE);
					if (chance >= bushChance)
					{
						continue;
					}

					BushInfo bush;
					if (!FindPlantGround(density, edits, column, x, z, boxMin, boxMax, bush.base))
					{
						continue;
					}
					bush.hash = h;
					bush.vegetation = &info;
					func(bush);
				}
			}
		}

		class VegetationBuilder
		{
		public:
			VegetationBuilder(const TerrainDensity& density, const EditList& edits, ChunkMeshData& out, i32 level) :
				m_Density(density),
				m_Edits(edits),
				m_Out(out),
				m_Level(level)
			{
			}

			// Appends the plants to the chunk's mesh: trunks (which are collided with) first, then foliage (which isn't)
			void Finish()
			{
				Append(m_Trunks);
				if (!m_Foliage.indices.empty())
				{
					m_Out.collisionIndexCount = (u32)m_Out.indices.size();
					Append(m_Foliage);
				}
			}

			// Adds the plants (& bushes) whose bases lie within the given box
			void Build(const glm::vec3& boxMin, const glm::vec3& boxMax)
			{
				ForEachPlant(m_Density, m_Edits, boxMin, boxMax, m_Level, [this](const PlantInfo& plant)
				{
					AddPlant(plant);
				});
				if (m_Level <= MAX_BUSH_LEVEL)
				{
					ForEachBush(m_Density, m_Edits, boxMin, boxMax, [this](const BushInfo& bush)
					{
						AddBush(bush);
					});
				}
			}

			// A single plant (at full detail)
			void BuildSingle(const PlantInfo& plant)
			{
				AddPlant(plant);
			}

		private:
			void Append(const ChunkMeshData& part)
			{
				const u32 base = (u32)m_Out.positions.size();
				m_Out.positions.insert(m_Out.positions.end(), part.positions.begin(), part.positions.end());
				m_Out.normals.insert(m_Out.normals.end(), part.normals.begin(), part.normals.end());
				m_Out.colours.insert(m_Out.colours.end(), part.colours.begin(), part.colours.end());
				m_Out.weights.insert(m_Out.weights.end(), part.weights.begin(), part.weights.end());
				for (u32 index : part.indices)
				{
					m_Out.indices.push_back(base + index);
				}
			}

			// Flat shaded triangle (one normal per face)
			void AddTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& colour, const glm::vec4& weights)
			{
				ChunkMeshData& out = *m_Target;
				glm::vec3 normal = glm::cross(b - a, c - a);
				const real length = glm::length(normal);
				if (length < 1e-8f)
				{
					return;
				}
				normal /= length;
				const u32 base = (u32)out.positions.size();
				for (const glm::vec3* p : { &a, &b, &c })
				{
					out.positions.push_back(*p);
					out.normals.push_back(normal);
					out.colours.push_back(glm::vec4(colour, 0.0f));
					out.weights.push_back(weights);
				}
				out.indices.push_back(base);
				out.indices.push_back(base + 1);
				out.indices.push_back(base + 2);
			}

			// A tapered prism (cone when topRadius is 0) around the vertical axis
			void AddFrustum(const glm::vec3& base, real height, real bottomRadius, real topRadius, u32 sides, real angleOffset,
				const glm::vec3& colour, const glm::vec4& weights, bool bCapBottom)
			{
				const glm::vec3 top = base + glm::vec3(0.0f, height, 0.0f);
				for (u32 i = 0; i < sides; ++i)
				{
					const real a0 = angleOffset + TWO_PI * (real)i / (real)sides;
					const real a1 = angleOffset + TWO_PI * (real)(i + 1) / (real)sides;
					const glm::vec3 d0(std::cos(a0), 0.0f, std::sin(a0));
					const glm::vec3 d1(std::cos(a1), 0.0f, std::sin(a1));
					const glm::vec3 b0 = base + d0 * bottomRadius;
					const glm::vec3 b1 = base + d1 * bottomRadius;
					if (topRadius > 0.0f)
					{
						const glm::vec3 t0 = top + d0 * topRadius;
						const glm::vec3 t1 = top + d1 * topRadius;
						AddTriangle(b0, t0, b1, colour, weights);
						AddTriangle(b1, t0, t1, colour, weights);
						AddTriangle(top, t1, t0, colour, weights);
					}
					else
					{
						AddTriangle(b0, top, b1, colour, weights);
					}
					if (bCapBottom)
					{
						AddTriangle(base, b0, b1, colour, weights);
					}
				}
			}

			// A tapered prism along any axis, closed at its far end
			void AddLimb(const glm::vec3& from, const glm::vec3& to, real fromRadius, real toRadius, u32 sides, const glm::vec3& colour, const glm::vec4& weights)
			{
				const glm::vec3 delta = to - from;
				const real length = glm::length(delta);
				if (length < 1e-4f)
				{
					return;
				}
				const glm::vec3 axis = delta / length;
				// (For a vertical axis these match AddFrustum's directions, so the winding is the same)
				const glm::vec3 reference = Abs(axis.y) < 0.95f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
				const glm::vec3 u = glm::normalize(glm::cross(reference, axis));
				const glm::vec3 v = glm::cross(axis, u);
				for (u32 i = 0; i < sides; ++i)
				{
					const real a0 = TWO_PI * (real)i / (real)sides;
					const real a1 = TWO_PI * (real)(i + 1) / (real)sides;
					const glm::vec3 d0 = v * std::cos(a0) + u * std::sin(a0);
					const glm::vec3 d1 = v * std::cos(a1) + u * std::sin(a1);
					const glm::vec3 b0 = from + d0 * fromRadius;
					const glm::vec3 b1 = from + d1 * fromRadius;
					const glm::vec3 t0 = to + d0 * toRadius;
					const glm::vec3 t1 = to + d1 * toRadius;
					AddTriangle(b0, t0, b1, colour, weights);
					AddTriangle(b1, t0, t1, colour, weights);
					AddTriangle(to, t1, t0, colour, weights);
				}
			}

			// A lumpy, low poly ball of leaves (octahedron, subdivided once when detailed)
			void AddBlob(const glm::vec3& center, const glm::vec3& radii, u32 hash, const glm::vec3& colour, const glm::vec4& weights, bool bSubdivide)
			{
				const glm::vec3 axes[6] = { glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(-1, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), glm::vec3(0, -1, 0) };
				const u32 faces[8][3] = { { 0, 4, 1 }, { 1, 4, 2 }, { 2, 4, 3 }, { 3, 4, 0 }, { 1, 5, 0 }, { 2, 5, 1 }, { 3, 5, 2 }, { 0, 5, 3 } };
				auto Point = [&](const glm::vec3& dir)
				{
					const glm::vec3 n = glm::normalize(dir);
					// Deterministic bumps
					const u32 bump = HashU32(hash + (u32)(i32)(n.x * 97.0f) * 31u + (u32)(i32)(n.y * 89.0f) * 17u + (u32)(i32)(n.z * 83.0f) * 7u);
					return center + n * radii * (0.85f + 0.3f * HashToUnit(bump));
				};
				for (const auto& face : faces)
				{
					const glm::vec3 a = axes[face[0]];
					const glm::vec3 b = axes[face[1]];
					const glm::vec3 c = axes[face[2]];
					if (bSubdivide)
					{
						const glm::vec3 ab = a + b;
						const glm::vec3 bc = b + c;
						const glm::vec3 ca = c + a;
						AddTriangle(Point(a), Point(ab), Point(ca), colour, weights);
						AddTriangle(Point(ab), Point(b), Point(bc), colour, weights);
						AddTriangle(Point(ca), Point(bc), Point(c), colour, weights);
						AddTriangle(Point(ab), Point(bc), Point(ca), colour, weights);
					}
					else
					{
						AddTriangle(Point(a), Point(b), Point(c), colour, weights);
					}
				}
			}

			void AddPlant(const PlantInfo& plant)
			{
				const glm::vec3 base = plant.base - glm::vec3(0.0f, 0.3f, 0.0f);
				const u32 hash = plant.hash;
				const real height = plant.height;
				const real trunkLength = plant.trunkHeight + 0.3f;
				const real trunkRadius = plant.trunkRadius;
				const real variation = HashToUnit(HashU32(hash + 9)) - 0.5f;
				const real angle = HashToUnit(HashU32(hash + 11)) * TWO_PI;
				// Weights: grass (leafy), layered rock, granular; w: sky visibility
				const glm::vec4 leafWeights(1.0f, 0.0f, 0.0f, 1.0f);
				const glm::vec4 barkWeights(0.0f, 0.2f, 0.8f, 1.0f);
				const glm::vec3 bark = glm::vec3(0.33f, 0.24f, 0.16f) * (1.0f + variation * 0.3f);
				const bool bDetailed = m_Level == 0;
				const u32 trunkSides = bDetailed ? 6 : 4;

				switch (plant.species)
				{
				case PlantSpecies::SPRUCE:
				{
					// Dark, dense & narrow, branches down to near the ground
					const glm::vec3 needles = glm::vec3(0.075f, 0.15f, 0.095f) * (1.0f + variation * 0.3f);
					m_Target = &m_Trunks;
					AddFrustum(base, trunkLength, trunkRadius, 0.15f, trunkSides, angle, bark * 0.85f, barkWeights, false);
					m_Target = &m_Foliage;
					const u32 tiers = bDetailed ? 3 : 1;
					for (u32 i = 0; i < tiers; ++i)
					{
						const real t = tiers == 1 ? 0.0f : (real)i / (real)(tiers - 1);
						const real tierBase = height * (0.16f + 0.3f * t);
						const real tierHeight = height * (tiers == 1 ? 0.84f : 0.54f - 0.12f * t);
						const real radius = height * (0.25f - 0.08f * t);
						AddFrustum(base + glm::vec3(0.0f, tierBase, 0.0f), tierHeight, radius, 0.0f, bDetailed ? 7 : 5, angle + (real)i * 0.4f, needles * (1.0f + 0.08f * (real)i), leafWeights, true);
					}
				} break;
				case PlantSpecies::PINE:
				{
					// Light green, tall bare trunk with a crown of short tiers
					const glm::vec3 needles = glm::vec3(0.17f, 0.29f, 0.12f) * (1.0f + variation * 0.3f);
					const glm::vec3 pineBark = glm::vec3(0.42f, 0.26f, 0.16f) * (1.0f + variation * 0.2f);
					m_Target = &m_Trunks;
					AddFrustum(base, trunkLength, trunkRadius, trunkRadius * 0.55f, trunkSides, angle, pineBark, barkWeights, false);
					m_Target = &m_Foliage;
					if (bDetailed)
					{
						const real tierBases[3] = { 0.45f, 0.62f, 0.76f };
						const real tierHeights[3] = { 0.3f, 0.26f, 0.24f };
						const real tierRadii[3] = { 0.21f, 0.17f, 0.11f };
						for (u32 i = 0; i < 3; ++i)
						{
							AddFrustum(base + glm::vec3(0.0f, height * tierBases[i], 0.0f), height * tierHeights[i], height * tierRadii[i], 0.0f, 7, angle + (real)i * 0.5f, needles * (1.0f + 0.07f * (real)i), leafWeights, true);
						}
					}
					else
					{
						AddFrustum(base + glm::vec3(0.0f, height * 0.45f, 0.0f), height * 0.55f, height * 0.2f, 0.0f, 5, angle, needles, leafWeights, true);
					}
				} break;
				case PlantSpecies::BIRCH:
				{
					// Slender white trunk, small airy light green canopy
					const glm::vec3 leaves = glm::vec3(0.30f, 0.42f, 0.13f) * (1.0f + variation * 0.3f);
					const glm::vec3 birchBark = glm::vec3(0.78f, 0.76f, 0.70f) * (1.0f + variation * 0.1f);
					m_Target = &m_Trunks;
					AddFrustum(base, trunkLength, trunkRadius, trunkRadius * 0.6f, trunkSides, angle, birchBark, barkWeights, false);
					m_Target = &m_Foliage;
					const real canopy = height * 0.2f;
					AddBlob(base + glm::vec3(0.0f, height * 0.72f, 0.0f), glm::vec3(canopy, canopy * 1.5f, canopy), hash, leaves, leafWeights, bDetailed);
					if (bDetailed)
					{
						const glm::vec3 offset(std::cos(angle) * canopy * 0.7f, 0.0f, std::sin(angle) * canopy * 0.7f);
						AddBlob(base + glm::vec3(0.0f, height * 0.6f, 0.0f) + offset, glm::vec3(canopy * 0.7f, canopy, canopy * 0.7f), hash + 1, leaves * 1.08f, leafWeights, false);
						AddBlob(base + glm::vec3(0.0f, height * 0.66f, 0.0f) - offset, glm::vec3(canopy * 0.65f, canopy * 0.9f, canopy * 0.65f), hash + 2, leaves * 0.93f, leafWeights, false);
					}
				} break;
				case PlantSpecies::OAK:
				{
					// Wide, light green canopy
					const glm::vec3 leaves = glm::vec3(0.22f, 0.36f, 0.11f) * (1.0f + variation * 0.35f);
					m_Target = &m_Trunks;
					AddFrustum(base, trunkLength, trunkRadius, 0.2f, trunkSides, angle, bark, barkWeights, false);
					m_Target = &m_Foliage;
					const real canopy = height * 0.33f;
					AddBlob(base + glm::vec3(0.0f, height * 0.68f, 0.0f), glm::vec3(canopy, canopy * 0.8f, canopy), hash, leaves, leafWeights, bDetailed);
					if (bDetailed)
					{
						const glm::vec3 offset(std::cos(angle) * canopy * 0.6f, 0.0f, std::sin(angle) * canopy * 0.6f);
						AddBlob(base + glm::vec3(0.0f, height * 0.58f, 0.0f) + offset, glm::vec3(canopy * 0.7f), hash + 1, leaves * 1.08f, leafWeights, true);
						AddBlob(base + glm::vec3(0.0f, height * 0.62f, 0.0f) - offset, glm::vec3(canopy * 0.65f), hash + 2, leaves * 0.93f, leafWeights, true);
					}
				} break;
				case PlantSpecies::WILLOW:
				{
					// Thick trunk under a drooping dark canopy
					const glm::vec3 leaves = glm::vec3(0.19f, 0.28f, 0.12f) * (1.0f + variation * 0.3f);
					const glm::vec3 willowBark = glm::vec3(0.30f, 0.26f, 0.20f) * (1.0f + variation * 0.2f);
					m_Target = &m_Trunks;
					AddFrustum(base, trunkLength, trunkRadius, trunkRadius * 0.7f, trunkSides, angle, willowBark, barkWeights, false);
					m_Target = &m_Foliage;
					const real canopy = height * 0.38f;
					AddBlob(base + glm::vec3(0.0f, height * 0.72f, 0.0f), glm::vec3(canopy, canopy * 0.55f, canopy), hash, leaves, leafWeights, bDetailed);
					// Hanging branches: a skirt from the canopy almost down to the ground
					AddFrustum(base + glm::vec3(0.0f, height * 0.18f, 0.0f), height * 0.5f, canopy * 1.05f, canopy * 0.8f, bDetailed ? 9 : 6, angle, leaves * 0.85f, leafWeights, false);
				} break;
				case PlantSpecies::ACACIA:
				{
					// Flat topped olive canopy on a tall trunk
					const glm::vec3 leaves = glm::vec3(0.27f, 0.32f, 0.12f) * (1.0f + variation * 0.3f);
					const glm::vec3 acaciaBark = glm::vec3(0.36f, 0.28f, 0.20f) * (1.0f + variation * 0.2f);
					m_Target = &m_Trunks;
					AddFrustum(base, trunkLength, trunkRadius, trunkRadius * 0.6f, trunkSides, angle, acaciaBark, barkWeights, false);
					m_Target = &m_Foliage;
					const real canopy = height * 0.48f;
					AddBlob(base + glm::vec3(0.0f, height * 0.84f, 0.0f), glm::vec3(canopy, canopy * 0.2f, canopy), hash, leaves, leafWeights, bDetailed);
					if (bDetailed)
					{
						const glm::vec3 offset(std::cos(angle) * canopy * 0.55f, 0.0f, std::sin(angle) * canopy * 0.55f);
						AddBlob(base + glm::vec3(0.0f, height * 0.78f, 0.0f) + offset, glm::vec3(canopy * 0.55f, canopy * 0.16f, canopy * 0.55f), hash + 1, leaves * 1.08f, leafWeights, false);
					}
				} break;
				case PlantSpecies::SAGUARO:
				{
					const glm::vec3 green = glm::vec3(0.24f, 0.36f, 0.2f) * (1.0f + variation * 0.3f);
					const glm::vec4 cactusWeights(0.3f, 0.0f, 0.7f, 1.0f);
					// Prickly: cacti are solid
					m_Target = &m_Trunks;
					const u32 sides = bDetailed ? 8 : 5;
					const real radius = trunkRadius;
					AddFrustum(base, height, radius, radius * 0.8f, sides, angle, green, cactusWeights, false);
					AddFrustum(base + glm::vec3(0.0f, height, 0.0f), radius * 0.5f, radius * 0.8f, 0.0f, sides, angle, green, cactusWeights, false);
					if (bDetailed)
					{
						// One or two arms: out, then up
						const u32 armCount = 1 + (HashU32(hash + 13) & 1);
						for (u32 i = 0; i < armCount; ++i)
						{
							const real armAngle = angle + PI * (real)i;
							const glm::vec3 dir(std::cos(armAngle), 0.0f, std::sin(armAngle));
							const real armHeight = height * (0.35f + 0.2f * HashToUnit(HashU32(hash + 20 + i)));
							const real armRadius = radius * 0.65f;
							const glm::vec3 elbow = base + glm::vec3(0.0f, armHeight, 0.0f) + dir * (radius + 0.45f);
							// Horizontal part as a short squat frustum around the elbow
							AddFrustum(elbow - glm::vec3(0.0f, armRadius, 0.0f) - dir * 0.45f, armRadius * 2.0f, armRadius, armRadius, sides, armAngle, green, cactusWeights, true);
							AddFrustum(elbow, height * 0.35f, armRadius, armRadius * 0.8f, sides, armAngle, green, cactusWeights, false);
							AddFrustum(elbow + glm::vec3(0.0f, height * 0.35f, 0.0f), armRadius * 0.5f, armRadius * 0.8f, 0.0f, sides, armAngle, green, cactusWeights, false);
						}
					}
				} break;
				case PlantSpecies::CHARRED:
				{
					// Blackened bark, with paler (ashy, splintered) broken ends
					const glm::vec3 charcoal = glm::vec3(0.075f, 0.068f, 0.062f) * (1.0f + variation * 0.4f);
					const glm::vec3 broken = glm::vec3(0.22f, 0.19f, 0.16f) * (1.0f + variation * 0.3f);
					const glm::vec4 charWeights(0.0f, 0.5f, 0.5f, 1.0f);
					const real radius = trunkRadius;
					m_Target = &m_Trunks;
					if (plant.size01 < 0.33f)
					{
						// Stump: flared base, ragged top
						AddFrustum(base, trunkLength, radius * 1.25f, radius, trunkSides, angle, charcoal, charWeights, false);
						AddFrustum(base + glm::vec3(0.0f, trunkLength, 0.0f), radius * 0.6f, radius, 0.0f, trunkSides, angle + 0.3f, broken, charWeights, false);
						break;
					}

					// Snag: a tapering trunk, snapped off at the top
					const real split = trunkLength * 0.55f;
					AddFrustum(base, split, radius * 1.2f, radius * 0.8f, trunkSides, angle, charcoal, charWeights, false);
					AddFrustum(base + glm::vec3(0.0f, split, 0.0f), trunkLength - split, radius * 0.8f, radius * 0.45f, trunkSides, angle, charcoal * 0.9f, charWeights, false);
					const glm::vec3 top = base + glm::vec3(0.0f, trunkLength, 0.0f);
					// Jagged splinters
					AddFrustum(top, radius * 2.5f, radius * 0.45f, 0.0f, bDetailed ? 5 : 3, angle + 0.7f, broken, charWeights, false);
					if (!bDetailed)
					{
						break;
					}

					// A few bare, burnt branch stubs (thin, so not collided with)
					m_Target = &m_Foliage;
					const u32 stubCount = 2 + HashU32(hash + 17) % 3;
					for (u32 i = 0; i < stubCount; ++i)
					{
						const u32 stubHash = HashU32(hash + 40 + i);
						const real t = 0.4f + 0.45f * HashToUnit(stubHash);
						const real stubAngle = angle + TWO_PI * ((real)i + 0.3f * HashToUnit(HashU32(stubHash + 1))) / (real)stubCount;
						const glm::vec3 out(std::cos(stubAngle), 0.0f, std::sin(stubAngle));
						const real stubRadius = radius * (0.8f - 0.35f * t) * 0.35f;
						const glm::vec3 from = base + glm::vec3(0.0f, trunkLength * t, 0.0f) + out * radius * 0.5f;
						const real length = (0.6f + 1.2f * HashToUnit(HashU32(stubHash + 2))) * (1.2f - t * 0.6f);
						const glm::vec3 to = from + glm::normalize(out + glm::vec3(0.0f, 0.5f + 0.5f * HashToUnit(HashU32(stubHash + 3)), 0.0f)) * length;
						AddLimb(from, to, stubRadius, stubRadius * 0.3f, 4, charcoal, charWeights);
					}
				} break;
				default:
					break;
				}
			}

			void AddBush(const BushInfo& bush)
			{
				const VegetationInfo& info = *bush.vegetation;
				const u32 hash = bush.hash;
				const real size = HashToUnit(HashU32(hash + 5));
				const real variation = HashToUnit(HashU32(hash + 9)) - 0.5f;
				const real angle = HashToUnit(HashU32(hash + 11)) * TWO_PI;
				const glm::vec4 leafWeights(1.0f, 0.0f, 0.0f, 1.0f);
				const bool bDetailed = m_Level == 0;
				const glm::vec3 base = bush.base - glm::vec3(0.0f, 0.15f, 0.0f);
				glm::vec3 colour = info.bushColour * (1.0f + variation * 0.35f);
				m_Target = &m_Foliage;

				real berryRadius = 0.0f;
				glm::vec3 berryCenter;
				switch (info.bushKind)
				{
				case BushKind::BERRY:
				{
					const real r = 0.45f + 0.35f * size;
					berryCenter = base + glm::vec3(0.0f, r * 0.7f, 0.0f);
					berryRadius = r;
					AddBlob(berryCenter, glm::vec3(r, r * 0.75f, r), hash, colour, leafWeights, bDetailed);
				} break;
				case BushKind::LEAFY:
				{
					const real r = 0.55f + 0.5f * size;
					berryCenter = base + glm::vec3(0.0f, r * 0.8f, 0.0f);
					berryRadius = r;
					AddBlob(berryCenter, glm::vec3(r, r * 0.85f, r), hash, colour, leafWeights, bDetailed);
					if (bDetailed)
					{
						const glm::vec3 offset(std::cos(angle) * r * 0.7f, 0.0f, std::sin(angle) * r * 0.7f);
						AddBlob(base + glm::vec3(0.0f, r * 0.55f, 0.0f) + offset, glm::vec3(r * 0.65f), hash + 1, colour * 1.1f, leafWeights, false);
					}
				} break;
				case BushKind::LOW_SHRUB:
				{
					// Low, wide mats, varying between greens, browns & the odd russet one
					const real r = 0.5f + 0.6f * size;
					const real tint = HashToUnit(HashU32(hash + 14));
					if (tint < 0.2f)
					{
						colour = glm::vec3(0.36f, 0.19f, 0.10f) * (1.0f + variation * 0.3f);
					}
					else if (tint < 0.5f)
					{
						colour = glm::mix(colour, glm::vec3(0.18f, 0.24f, 0.11f), 0.6f);
					}
					berryCenter = base + glm::vec3(0.0f, r * 0.25f, 0.0f);
					berryRadius = r;
					AddBlob(berryCenter, glm::vec3(r, r * 0.38f, r * 0.85f), hash, colour, leafWeights, bDetailed);
					if (bDetailed)
					{
						const glm::vec3 offset(std::cos(angle) * r * 0.8f, 0.0f, std::sin(angle) * r * 0.8f);
						AddBlob(base + offset + glm::vec3(0.0f, r * 0.15f, 0.0f), glm::vec3(r * 0.6f, r * 0.3f, r * 0.6f), hash + 1, colour * 0.9f, leafWeights, false);
					}
				} break;
				case BushKind::SCRUB:
				{
					// A dry tuft: a low clump with a few stiff spikes
					const real r = 0.3f + 0.35f * size;
					AddBlob(base + glm::vec3(0.0f, r * 0.35f, 0.0f), glm::vec3(r, r * 0.5f, r), hash, colour, leafWeights, false);
					if (bDetailed)
					{
						const u32 spikes = 4 + (HashU32(hash + 15) % 3);
						for (u32 i = 0; i < spikes; ++i)
						{
							const real a = angle + TWO_PI * (real)i / (real)spikes;
							const real d = r * (0.2f + 0.5f * HashToUnit(HashU32(hash + 40 + i)));
							const real spikeHeight = r * (1.4f + 1.4f * HashToUnit(HashU32(hash + 50 + i)));
							const glm::vec3 spikeBase = base + glm::vec3(std::cos(a) * d, 0.0f, std::sin(a) * d);
							AddFrustum(spikeBase, spikeHeight, 0.035f + 0.02f * size, 0.0f, 3, a, colour * 1.2f, leafWeights, false);
						}
					}
				} break;
				}

				// Berries dotted over the top of the bush
				if (bDetailed && berryRadius > 0.0f && HashToUnit(HashU32(hash + 16)) < info.berryFraction)
				{
					const u32 berries = 4 + (HashU32(hash + 17) % 4);
					const real berrySize = 0.04f;
					for (u32 i = 0; i < berries; ++i)
					{
						const real a = TWO_PI * HashToUnit(HashU32(hash + 60 + i));
						const real up = 0.15f + 0.6f * HashToUnit(HashU32(hash + 70 + i));
						const glm::vec3 dir = glm::normalize(glm::vec3(std::cos(a), up, std::sin(a)));
						const real verticalScale = info.bushKind == BushKind::LOW_SHRUB ? 0.38f : 0.8f;
						const glm::vec3 pos = berryCenter + glm::vec3(dir.x * berryRadius, dir.y * berryRadius * verticalScale, dir.z * berryRadius) * 0.98f;
						AddBlob(pos, glm::vec3(berrySize), hash + 80 + i, info.berryColour, leafWeights, false);
					}
				}
			}

			const TerrainDensity& m_Density;
			const EditList& m_Edits;
			ChunkMeshData& m_Out;
			ChunkMeshData m_Trunks;
			ChunkMeshData m_Foliage;
			ChunkMeshData* m_Target = &m_Trunks;
			i32 m_Level;
		};

		class ChunkMesher
		{
		public:
			ChunkMesher(const TerrainDensity& density, const ChunkKey& key, TransitionMask mask, const EditList& edits, ChunkMeshData& outMesh) :
				m_Density(density),
				m_Key(key),
				m_Mask(mask),
				m_Edits(edits),
				m_Out(outMesh)
			{
				const WorldParams& params = density.Params();
				N = (i32)params.cellsPerChunkAxis;
				G = N + 3;
				m_Step = 1 << key.level;
				m_Origin = glm::i64vec3(key.index) * (i64)(N * m_Step);
				m_VoxelSize = params.voxelSize;
				m_CellWorldSize = m_VoxelSize * (real)m_Step;
				m_HalfCellWorldSize = m_VoxelSize * (real)m_Step * 0.5f;
			}

			void Run()
			{
				m_Out.Clear();

				// Columns (2D samples) on the grid, including one cell of padding for gradients. These are identical for
				// all chunks in a vertical stack, so are shared between them.
				ColumnGridCache& columnCache = m_Density.ColumnCache();
				m_GridColumns = columnCache.Find(m_Key.level, m_Key.index.x, m_Key.index.z);
				if (m_GridColumns == nullptr)
				{
					std::shared_ptr<std::vector<ColumnSample>> columns = std::make_shared<std::vector<ColumnSample>>((size_t)G * G);
					for (i32 i = -1; i <= N + 1; ++i)
					{
						for (i32 k = -1; k <= N + 1; ++k)
						{
							const glm::vec3 p = WorldPos(glm::ivec3(i * 2, 0, k * 2));
							(*columns)[GridColumnIndex(i, k)] = m_Density.SampleColumn(p.x, p.z);
						}
					}
					m_GridColumns = columns;
					columnCache.Insert(m_Key.level, m_Key.index.x, m_Key.index.z, m_GridColumns);
				}
				m_Columns = m_GridColumns->data();

				real minHeight = 1e30f;
				real maxHeight = -1e30f;
				for (i32 i = 0; i < G * G; ++i)
				{
					minHeight = glm::min(minHeight, m_Columns[i].height);
					maxHeight = glm::max(maxHeight, m_Columns[i].height);
				}

				// The water level may lie in a chunk without any terrain, so this must happen before early outs
				GenerateWater();

				// Early out on chunks that are entirely above or below the surface (edits can place surfaces anywhere)
				const real chunkMinY = WorldPos(glm::ivec3(0, -2, 0)).y;
				const real chunkMaxY = WorldPos(glm::ivec3(0, 2 * N + 2, 0)).y;
				const real margin = m_CellWorldSize * 2.0f;
				if (m_Edits.count == 0 &&
					(chunkMinY > maxHeight + m_Density.MaxExtentAbove() + margin ||
					chunkMaxY < minHeight - m_Density.MaxExtentBelow() - margin))
				{
					return;
				}

				m_GridDensities.resize((size_t)G * G * G);
				m_Densities = m_GridDensities.data();
				i32 insideCount = 0;
				for (i32 i = -1; i <= N + 1; ++i)
				{
					for (i32 k = -1; k <= N + 1; ++k)
					{
						const ColumnSample& column = m_Columns[GridColumnIndex(i, k)];
						for (i32 j = -1; j <= N + 1; ++j)
						{
							const glm::vec3 p = WorldPos(glm::ivec3(i * 2, j * 2, k * 2));
							const real d = m_Density.Sample(p, column, m_Edits);
							m_Densities[GridIndex(i, j, k)] = d;
							if (i >= 0 && i <= N && j >= 0 && j <= N && k >= 0 && k <= N && d < 0.0f)
							{
								++insideCount;
							}
						}
					}
				}

				const i32 pointCount = (N + 1) * (N + 1) * (N + 1);
				const bool bUniform = (insideCount == 0 || insideCount == pointCount);
				if (bUniform && m_Mask == 0)
				{
					return;
				}

				for (i32 i = 0; i < N; ++i)
				{
					for (i32 j = 0; j < N; ++j)
					{
						for (i32 k = 0; k < N; ++k)
						{
							const glm::ivec3 cellMin(i * 2, j * 2, k * 2);
							const bool bTransition = m_Mask != 0 && IsTransitionCell(cellMin);
							if (bTransition)
							{
								ProcessTransitionCell(cellMin);
							}
							else if (!bUniform)
							{
								ProcessRegularCell(i, j, k);
							}
						}
					}
				}

				// Plants are only shown up close (they're too small to see from far away)
				if (!bUniform && m_Key.level <= MAX_VEGETATION_LEVEL)
				{
					VegetationBuilder vegetation(m_Density, m_Edits, m_Out, m_Key.level);
					vegetation.Build(WorldPos(glm::ivec3(0, 0, 0)), WorldPos(glm::ivec3(2 * N, 2 * N, 2 * N)));
					vegetation.Finish();
				}
			}

			static const i32 MAX_VEGETATION_LEVEL = 2;

		private:
			struct PointSample
			{
				real density;
				bool bGradientValid;
				glm::vec3 gradient;
			};

			inline size_t GridIndex(i32 i, i32 j, i32 k) const
			{
				return ((size_t)(i + 1) * G + (size_t)(j + 1)) * G + (size_t)(k + 1);
			}

			inline size_t GridColumnIndex(i32 i, i32 k) const
			{
				return (size_t)(i + 1) * G + (size_t)(k + 1);
			}

			static inline u32 PointKey(const glm::ivec3& a)
			{
				return (u32)(a.x + 4) | ((u32)(a.y + 4) << 10) | ((u32)(a.z + 4) << 20);
			}

			static inline bool IsEven(const glm::ivec3& a)
			{
				return ((a.x | a.y | a.z) & 1) == 0;
			}

			// The heightfield gives a cheap first guess (points far below it are probably in caves), which is checked by
			// looking for rock above the point: the heightfield can be well above the real surface (overhangs, carved
			// lakes, coarse LODs), & points with open air above them are lit no matter how deep they are. Looks up &
			// away from the surface, so cliff faces (which have rock straight above them) aren't mistaken for caves,
			// while cave floors (ceiling above) & walls (the opposite wall or ceiling) still are.
			real ComputeSkyVisibility(const glm::vec3& pos, const glm::vec3& normal, const ColumnSample& column) const
			{
				// Coarse chunks' vertices can be far from the real surface (even inside it), & caves can't be seen from
				// the distances they're shown at anyway
				if (m_Key.level >= 2)
				{
					return 1.0f;
				}
				const real estimate = SkyVisibility(pos, column);
				if (estimate >= 0.999f)
				{
					return 1.0f;
				}
				// Several rays fanning out above the surface: crevices (where some can escape) are only partly darkened,
				// enclosed spaces like caves fully
				const glm::vec3 up = glm::normalize(normal + glm::vec3(0.0f, 1.0f, 0.0f) + glm::vec3(0.0f, 1e-3f, 0.0f));
				const glm::vec3 side0 = glm::normalize(glm::cross(up, glm::abs(up.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f)));
				const glm::vec3 side1 = glm::cross(up, side0);
				const glm::vec3 dirs[5] = { up, glm::normalize(up + side0 * 0.8f), glm::normalize(up - side0 * 0.8f), glm::normalize(up + side1 * 0.8f), glm::normalize(up - side1 * 0.8f) };
				u32 blockedCount = 0;
				for (const glm::vec3& dir : dirs)
				{
					// Short enough that rays from small hollows in open slopes escape, but cave ceilings & walls block them
					for (real distance : { 6.0f, 16.0f, 34.0f })
					{
						// (sampled with its own column, which can differ a lot from the vertex's on steep terrain)
						if (m_Density.Sample(pos + normal * m_CellWorldSize + dir * distance, m_Edits) < 0.0f)
						{
							++blockedCount;
							break;
						}
					}
				}
				const real blocked = (real)blockedCount / 5.0f;
				return 1.0f - (1.0f - estimate) * blocked * blocked;
			}

			glm::vec3 WorldPos(const glm::ivec3& a) const
			{
				// Computed from integers so positions shared between chunks (of any level) are bit-identical
				const i64 x = m_Origin.x * 2 + (i64)a.x * m_Step;
				const i64 y = m_Origin.y * 2 + (i64)a.y * m_Step;
				const i64 z = m_Origin.z * 2 + (i64)a.z * m_Step;
				return glm::vec3((real)x * 0.5f * m_VoxelSize, (real)y * 0.5f * m_VoxelSize, (real)z * 0.5f * m_VoxelSize);
			}

			const ColumnSample& Column(i32 ax, i32 az)
			{
				if (((ax | az) & 1) == 0)
				{
					return m_Columns[GridColumnIndex(ax / 2, az / 2)];
				}

				const u32 key = (u32)(ax + 4) | ((u32)(az + 4) << 16);
				ColumnSample* existing = m_ExtraColumns.Find(key);
				if (existing != nullptr)
				{
					return *existing;
				}
				const glm::vec3 p = WorldPos(glm::ivec3(ax, 0, az));
				return m_ExtraColumns.Insert(key, m_Density.SampleColumn(p.x, p.z));
			}

			real Density(const glm::ivec3& a)
			{
				if (IsEven(a))
				{
					return m_Densities[GridIndex(a.x / 2, a.y / 2, a.z / 2)];
				}

				const u32 key = PointKey(a);
				real* existing = m_ExtraDensities.Find(key);
				if (existing != nullptr)
				{
					return *existing;
				}
				const real d = m_Density.Sample(WorldPos(a), Column(a.x, a.z), m_Edits);
				m_ExtraDensities.Insert(key, d);
				return d;
			}

			bool IsOnBoundary(i32 v) const
			{
				return v == 0 || v == 2 * N;
			}

			// Whether a point lies on a chunk face or edge shared with a finer chunk
			bool IsOnSplitBoundary(const glm::ivec3& a) const
			{
				for (u32 axis = 0; axis < 3; ++axis)
				{
					if (IsOnBoundary(a[axis]) && (m_Mask & FaceBit(axis, a[axis] != 0 ? 1 : 0)))
					{
						return true;
					}
				}
				for (u32 axis = 0; axis < 3; ++axis)
				{
					const u32 o0 = (axis + 1) % 3 < (axis + 2) % 3 ? (axis + 1) % 3 : (axis + 2) % 3;
					const u32 o1 = (axis + 1) % 3 < (axis + 2) % 3 ? (axis + 2) % 3 : (axis + 1) % 3;
					if (IsOnBoundary(a[o0]) && IsOnBoundary(a[o1]) &&
						(m_Mask & EdgeBit(axis, a[o0] != 0 ? 1 : 0, a[o1] != 0 ? 1 : 0)))
					{
						return true;
					}
				}
				return false;
			}

			// a & b form an axis aligned cell edge
			bool IsEdgeSplit(const glm::ivec3& a, const glm::ivec3& b) const
			{
				const u32 axis = a.x != b.x ? 0 : (a.y != b.y ? 1 : 2);
				const u32 o0 = axis == 0 ? 1 : 0;
				const u32 o1 = axis == 2 ? 1 : 2;
				const bool bOn0 = IsOnBoundary(a[o0]);
				const bool bOn1 = IsOnBoundary(a[o1]);
				if (bOn0 && (m_Mask & FaceBit(o0, a[o0] != 0 ? 1 : 0)))
				{
					return true;
				}
				if (bOn1 && (m_Mask & FaceBit(o1, a[o1] != 0 ? 1 : 0)))
				{
					return true;
				}
				if (bOn0 && bOn1 && (m_Mask & EdgeBit(axis, a[o0] != 0 ? 1 : 0, a[o1] != 0 ? 1 : 0)))
				{
					return true;
				}
				return false;
			}

			bool IsTransitionCell(const glm::ivec3& cellMin) const
			{
				// Only cells touching the chunk boundary can be transition cells
				bool bOnBoundary = false;
				for (u32 axis = 0; axis < 3; ++axis)
				{
					bOnBoundary = bOnBoundary || cellMin[axis] == 0 || cellMin[axis] + 2 == 2 * N;
				}
				if (!bOnBoundary)
				{
					return false;
				}

				// Check all 12 edges
				for (u32 axis = 0; axis < 3; ++axis)
				{
					const u32 o0 = axis == 0 ? 1 : 0;
					const u32 o1 = axis == 2 ? 1 : 2;
					for (i32 s0 = 0; s0 <= 1; ++s0)
					{
						for (i32 s1 = 0; s1 <= 1; ++s1)
						{
							glm::ivec3 a = cellMin;
							a[o0] += s0 * 2;
							a[o1] += s1 * 2;
							glm::ivec3 b = a;
							b[axis] += 2;
							if (IsEdgeSplit(a, b))
							{
								return true;
							}
						}
					}
				}
				return false;
			}

			glm::vec3 Gradient(const glm::ivec3& a)
			{
				if (IsEven(a) && !IsOnSplitBoundary(a))
				{
					const i32 i = a.x / 2;
					const i32 j = a.y / 2;
					const i32 k = a.z / 2;
					const real inv = 1.0f / (2.0f * m_CellWorldSize);
					return glm::vec3(
						m_Densities[GridIndex(i + 1, j, k)] - m_Densities[GridIndex(i - 1, j, k)],
						m_Densities[GridIndex(i, j + 1, k)] - m_Densities[GridIndex(i, j - 1, k)],
						m_Densities[GridIndex(i, j, k + 1)] - m_Densities[GridIndex(i, j, k - 1)]) * inv;
				}

				// Points shared with a finer chunk use the finer chunk's sample spacing so normals match across the seam
				const u32 key = PointKey(a);
				glm::vec3* existing = m_ExtraGradients.Find(key);
				if (existing != nullptr)
				{
					return *existing;
				}
				const real inv = 1.0f / (2.0f * m_HalfCellWorldSize);
				const glm::vec3 gradient = glm::vec3(
					Density(a + glm::ivec3(1, 0, 0)) - Density(a - glm::ivec3(1, 0, 0)),
					Density(a + glm::ivec3(0, 1, 0)) - Density(a - glm::ivec3(0, 1, 0)),
					Density(a + glm::ivec3(0, 0, 1)) - Density(a - glm::ivec3(0, 0, 1))) * inv;
				m_ExtraGradients.Insert(key, gradient);
				return gradient;
			}

			u32 GetEdgeVertex(glm::ivec3 a, real da, glm::ivec3 b, real db)
			{
				u32 keyA = PointKey(a);
				u32 keyB = PointKey(b);
				// Canonical ordering so shared edges produce bit-identical vertices in every chunk
				if (keyA > keyB)
				{
					std::swap(a, b);
					std::swap(da, db);
					std::swap(keyA, keyB);
				}

				const u64 edgeKey = ((u64)keyA << 32) | (u64)keyB;
				u32* existing = m_VertexMap.Find(edgeKey);
				if (existing != nullptr)
				{
					return *existing;
				}

				const real t = glm::clamp(da / (da - db), 0.0f, 1.0f);
				const glm::vec3 pa = WorldPos(a);
				const glm::vec3 pb = WorldPos(b);
				const glm::vec3 pos = pa + (pb - pa) * t;

				const glm::vec3 ga = Gradient(a);
				const glm::vec3 gb = Gradient(b);
				glm::vec3 normal = ga + (gb - ga) * t;
				const real normalLen = glm::length(normal);
				normal = normalLen > 1e-12f ? normal / normalLen : glm::vec3(0.0f, 1.0f, 0.0f);

				const ColumnSample& ca = Column(a.x, a.z);
				const ColumnSample& cb = Column(b.x, b.z);
				const ColumnSample& column = (t < 0.5f) ? ca : cb;
				ColumnSample blended = column;
				blended.height = ca.height + (cb.height - ca.height) * t;
				const SurfaceAttributes surface = m_Density.ComputeSurface(pos, normal, blended, m_Edits);

				const u32 index = (u32)m_Out.positions.size();
				m_Out.positions.push_back(pos);
				m_Out.normals.push_back(normal);
				m_Out.colours.push_back(surface.colour);
				m_Out.weights.push_back(PackVertexWeights(surface.weights, ComputeSkyVisibility(pos, normal, blended)));
				m_VertexMap.Insert(edgeKey, index);
				return index;
			}

			// outwardDir points from the inside of the surface towards the outside
			void EmitTriangle(u32 i0, u32 i1, u32 i2, const glm::vec3& outwardDir)
			{
				if (i0 == i1 || i1 == i2 || i0 == i2)
				{
					return;
				}

				const glm::vec3& p0 = m_Out.positions[i0];
				const glm::vec3& p1 = m_Out.positions[i1];
				const glm::vec3& p2 = m_Out.positions[i2];
				const glm::vec3 cross = glm::cross(p1 - p0, p2 - p0);
				if (glm::dot(cross, outwardDir) < 0.0f)
				{
					std::swap(i1, i2);
				}

				m_Out.indices.push_back(i0);
				m_Out.indices.push_back(i1);
				m_Out.indices.push_back(i2);
			}

			void ProcessTet(const glm::ivec3& p0, const glm::ivec3& p1, const glm::ivec3& p2, const glm::ivec3& p3)
			{
				const glm::ivec3 p[4] = { p0, p1, p2, p3 };
				real d[4];
				u32 insideMask = 0;
				for (u32 i = 0; i < 4; ++i)
				{
					d[i] = Density(p[i]);
					if (d[i] < 0.0f)
					{
						insideMask |= (1u << i);
					}
				}

				if (insideMask == 0 || insideMask == 0xF)
				{
					return;
				}

				u32 inside[4];
				u32 outside[4];
				u32 insideCount = 0;
				u32 outsideCount = 0;
				for (u32 i = 0; i < 4; ++i)
				{
					if (insideMask & (1u << i))
					{
						inside[insideCount++] = i;
					}
					else
					{
						outside[outsideCount++] = i;
					}
				}

				auto Vert = [&](u32 i, u32 j)
				{
					return GetEdgeVertex(p[i], d[i], p[j], d[j]);
				};

				// Orient using the tetrahedron's geometry (inside corners -> outside corners), which is exact and
				// consistent between neighboring tetrahedra, unlike the interpolated gradient
				glm::vec3 insideCentroid(0.0f);
				glm::vec3 outsideCentroid(0.0f);
				for (u32 i = 0; i < insideCount; ++i)
				{
					insideCentroid += glm::vec3(p[inside[i]]);
				}
				for (u32 i = 0; i < outsideCount; ++i)
				{
					outsideCentroid += glm::vec3(p[outside[i]]);
				}
				const glm::vec3 outwardDir = outsideCentroid / (real)outsideCount - insideCentroid / (real)insideCount;

				if (insideCount == 1 || insideCount == 3)
				{
					const u32 lone = insideCount == 1 ? inside[0] : outside[0];
					const u32* others = insideCount == 1 ? outside : inside;
					EmitTriangle(Vert(lone, others[0]), Vert(lone, others[1]), Vert(lone, others[2]), outwardDir);
				}
				else
				{
					const u32 v00 = Vert(inside[0], outside[0]);
					const u32 v01 = Vert(inside[0], outside[1]);
					const u32 v11 = Vert(inside[1], outside[1]);
					const u32 v10 = Vert(inside[1], outside[0]);
					EmitTriangle(v00, v01, v11, outwardDir);
					EmitTriangle(v00, v11, v10, outwardDir);
				}
			}

			void ProcessRegularCell(i32 i, i32 j, i32 k)
			{
				// Skip cells entirely inside or outside
				u32 insideCount = 0;
				for (i32 c = 0; c < 8; ++c)
				{
					if (m_Densities[GridIndex(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1))] < 0.0f)
					{
						++insideCount;
					}
				}
				if (insideCount == 0 || insideCount == 8)
				{
					return;
				}

				const glm::ivec3 base(i * 2, j * 2, k * 2);
				auto Corner = [&](u32 c)
				{
					return base + glm::ivec3((c & 1) * 2, ((c >> 1) & 1) * 2, ((c >> 2) & 1) * 2);
				};

				static const u32 axisPerms[6][2] = { { 0, 1 }, { 0, 2 }, { 1, 0 }, { 1, 2 }, { 2, 0 }, { 2, 1 } };
				for (u32 t = 0; t < 6; ++t)
				{
					const u32 c1 = 1u << axisPerms[t][0];
					const u32 c2 = c1 | (1u << axisPerms[t][1]);
					ProcessTet(Corner(0), Corner(c1), Corner(c2), Corner(7));
				}
			}

			void ProcessTransitionCell(const glm::ivec3& cellMin)
			{
				const glm::ivec3 center = cellMin + glm::ivec3(1);

				for (u32 axis = 0; axis < 3; ++axis)
				{
					const u32 u = axis == 0 ? 1 : 0;
					const u32 v = axis == 2 ? 1 : 2;
					const glm::ivec3 eu = glm::ivec3(u == 0, u == 1, u == 2);
					const glm::ivec3 ev = glm::ivec3(v == 0, v == 1, v == 2);

					for (i32 side = 0; side <= 1; ++side)
					{
						glm::ivec3 faceMin = cellMin;
						faceMin[axis] += side * 2;

						const bool bFaceSplit = IsOnBoundary(faceMin[axis]) && (m_Mask & FaceBit(axis, faceMin[axis] != 0 ? 1 : 0));
						if (bFaceSplit)
						{
							// Match the finer neighbor's 2x2 cells
							for (i32 fi = 0; fi < 2; ++fi)
							{
								for (i32 fj = 0; fj < 2; ++fj)
								{
									const glm::ivec3 q00 = faceMin + eu * fi + ev * fj;
									const glm::ivec3 q10 = q00 + eu;
									const glm::ivec3 q11 = q00 + eu + ev;
									const glm::ivec3 q01 = q00 + ev;
									ProcessTet(center, q00, q10, q11);
									ProcessTet(center, q00, q11, q01);
								}
							}
							continue;
						}

						const glm::ivec3 c00 = faceMin;
						const glm::ivec3 c10 = faceMin + eu * 2;
						const glm::ivec3 c11 = faceMin + eu * 2 + ev * 2;
						const glm::ivec3 c01 = faceMin + ev * 2;

						glm::ivec3 loop[8];
						u32 loopCount = 0;
						const glm::ivec3 corners[4] = { c00, c10, c11, c01 };
						for (u32 c = 0; c < 4; ++c)
						{
							const glm::ivec3& a = corners[c];
							const glm::ivec3& b = corners[(c + 1) % 4];
							loop[loopCount++] = a;
							if (IsEdgeSplit(a, b))
							{
								loop[loopCount++] = (a + b) / 2;
							}
						}

						if (loopCount == 4)
						{
							// Same diagonal as regular cells use
							ProcessTet(center, c00, c10, c11);
							ProcessTet(center, c00, c11, c01);
						}
						else
						{
							const glm::ivec3 faceCenter = faceMin + eu + ev;
							for (u32 l = 0; l < loopCount; ++l)
							{
								ProcessTet(center, faceCenter, loop[l], loop[(l + 1) % loopCount]);
							}
						}
					}
				}
			}

			// Density (> 0: air) of the terrain at height y above grid column (i, k), interpolated between the grid points
			// above & below it like the chunk's mesh is (so water meets the terrain where its surface crosses the water
			// level at this LOD)
			real TerrainAirAtLevel(i32 i, i32 k, real y)
			{
				const size_t columnIndex = GridColumnIndex(i, k);
				if (m_WaterLevelDensities.empty())
				{
					m_WaterLevelDensities.assign((size_t)G * G, glm::vec2(0.0f, -1e30f));
				}
				glm::vec2& cached = m_WaterLevelDensities[columnIndex];
				if (cached.y == y)
				{
					return cached.x;
				}

				const real chunkMinY = WorldPos(glm::ivec3(0, 0, 0)).y;
				const i32 j = glm::clamp((i32)std::floor((y - chunkMinY) / m_CellWorldSize), 0, N - 1);
				const ColumnSample& column = m_Columns[columnIndex];
				const glm::vec3 p0 = WorldPos(glm::ivec3(i * 2, j * 2, k * 2));
				const glm::vec3 p1 = WorldPos(glm::ivec3(i * 2, j * 2 + 2, k * 2));
				// (the same values as the density grid's, which is filled later)
				const real d0 = m_Density.Sample(p0, column, m_Edits);
				const real d1 = m_Density.Sample(p1, column, m_Edits);
				const real t = glm::clamp((y - p0.y) / (p1.y - p0.y), 0.0f, 1.0f);
				cached = glm::vec2(d0 + (d1 - d0) * t, y);
				return cached.x;
			}

			// Flat lake surfaces, outlined with marching squares on the lake's shore field (lakeT == 1), clipped where the
			// terrain is above the water level
			void GenerateWater()
			{
				const real chunkMinY = WorldPos(glm::ivec3(0, 0, 0)).y;
				const real chunkMaxY = WorldPos(glm::ivec3(0, 2 * N, 0)).y;

				for (i32 i = 0; i < N; ++i)
				{
					for (i32 k = 0; k < N; ++k)
					{
						const ColumnSample* corners[4] =
						{
							&m_Columns[GridColumnIndex(i, k)],
							&m_Columns[GridColumnIndex(i + 1, k)],
							&m_Columns[GridColumnIndex(i + 1, k + 1)],
							&m_Columns[GridColumnIndex(i, k + 1)],
						};

						u32 lakeID = 0;
						real level = 0.0f;
						real nearestT = 1.0f;
						for (const ColumnSample* corner : corners)
						{
							if (corner->lakeID != 0 && corner->lakeT < nearestT)
							{
								nearestT = corner->lakeT;
								lakeID = corner->lakeID;
								level = corner->waterLevel;
							}
						}

						// Each water surface belongs to the chunk containing its level
						if (lakeID == 0 || level < chunkMinY || level >= chunkMaxY)
						{
							continue;
						}

						real values[4];
						glm::vec2 positions[4];
						static const i32 cornerOffsets[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
						for (u32 c = 0; c < 4; ++c)
						{
							values[c] = corners[c]->lakeID == lakeID ? corners[c]->lakeT - 1.0f : 1.0f;
							if (values[c] < 0.0f)
							{
								// Water is also clipped where the terrain rises above it (e.g. beaches, which the shore field
								// doesn't follow exactly), so it doesn't show through the land
								values[c] = Max(values[c], -TerrainAirAtLevel(i + cornerOffsets[c][0], k + cornerOffsets[c][1], level) / m_CellWorldSize);
							}
							const glm::vec3 p = WorldPos(glm::ivec3((i + cornerOffsets[c][0]) * 2, 0, (k + cornerOffsets[c][1]) * 2));
							positions[c] = glm::vec2(p.x, p.z);
						}

						glm::vec2 polygon[8];
						u32 polygonCount = 0;
						for (u32 c = 0; c < 4; ++c)
						{
							const u32 next = (c + 1) % 4;
							if (values[c] < 0.0f)
							{
								polygon[polygonCount++] = positions[c];
							}
							if ((values[c] < 0.0f) != (values[next] < 0.0f))
							{
								const real t = values[c] / (values[c] - values[next]);
								polygon[polygonCount++] = positions[c] + (positions[next] - positions[c]) * t;
							}
						}

						if (polygonCount < 3)
						{
							continue;
						}

						const u32 baseIndex = (u32)m_Out.waterPositions.size();
						for (u32 v = 0; v < polygonCount; ++v)
						{
							m_Out.waterPositions.emplace_back(polygon[v].x, level, polygon[v].y);
						}
						for (u32 v = 1; v + 1 < polygonCount; ++v)
						{
							m_Out.waterIndices.push_back(baseIndex);
							m_Out.waterIndices.push_back(baseIndex + v + 1);
							m_Out.waterIndices.push_back(baseIndex + v);
						}
					}
				}
			}

			const TerrainDensity& m_Density;
			const ChunkKey m_Key;
			const TransitionMask m_Mask;
			const EditList m_Edits;
			ChunkMeshData& m_Out;

			i32 N;
			i32 G;
			i32 m_Step;
			glm::i64vec3 m_Origin;
			real m_VoxelSize;
			real m_CellWorldSize;
			real m_HalfCellWorldSize;

			std::shared_ptr<const std::vector<ColumnSample>> m_GridColumns;
			std::vector<real> m_GridDensities;
			// Raw pointers into the above vectors, to avoid debug iterator checks in hot loops
			const ColumnSample* m_Columns = nullptr;
			real* m_Densities = nullptr;

			FlatHashMap<u32, ColumnSample> m_ExtraColumns;
			FlatHashMap<u32, real> m_ExtraDensities;
			FlatHashMap<u32, glm::vec3> m_ExtraGradients;
			FlatHashMap<u64, u32> m_VertexMap;
			// Per grid column: x: terrain density at the water level y (see TerrainAirAtLevel)
			std::vector<glm::vec2> m_WaterLevelDensities;
		};

		void GenerateChunkMesh(const TerrainDensity& density, const ChunkKey& key, TransitionMask mask, const EditList& edits, ChunkMeshData& outMesh)
		{
			ChunkMesher mesher(density, key, mask, edits, outMesh);
			mesher.Run();
		}

		void FindPlants(const TerrainDensity& density, const EditList& edits, const glm::vec3& boxMin, const glm::vec3& boxMax, std::vector<PlantInfo>& outPlants)
		{
			ForEachPlant(density, edits, boxMin, boxMax, 0, [&outPlants](const PlantInfo& plant)
			{
				outPlants.push_back(plant);
			});
		}

		void BuildPlantMesh(const TerrainDensity& density, const PlantInfo& plant, ChunkMeshData& outMesh)
		{
			outMesh.Clear();
			const EditList noEdits = {};
			VegetationBuilder builder(density, noEdits, outMesh, 0);
			builder.BuildSingle(plant);
			builder.Finish();
			if (outMesh.collisionIndexCount == ~0u)
			{
				outMesh.collisionIndexCount = (u32)outMesh.indices.size();
			}
		}

		Edit MakeRemovePlantEdit(const PlantInfo& plant)
		{
			Edit edit = {};
			edit.center = plant.base;
			// Large enough that the chunk the plant was generated in (& any it could later be found in after digging
			// below it) gathers this edit
			edit.radius = 3.0f;
			edit.type = EditType::REMOVE_PLANT;
			edit.material = Material::SOIL;
			return edit;
		}

		SpawnSearchResult FindSpawnPoint(const TerrainDensity& density, real maxRadius /* = 20000.0f */)
		{
			const real seaLevel = density.Params().seaLevel;
			// Low ground (lowlands are flatter & easier to start out on), but clear of beaches & shallows. Dry land below
			// sea level (inland basins) is fine, just less preferred.
			const real idealHeight = seaLevel + 12.0f;
			const real minHeight = seaLevel - 80.0f;
			const real maxHeight = seaLevel + 220.0f;
			const real maxSlope = 0.2f; // Rise over run (~11 degrees)
			// Score units: one per this many meters above/below the ideal height, of slope & from the origin
			const real heightAboveScale = 50.0f;
			const real heightBelowScale = 100.0f;
			const real slopeScale = 0.1f;
			const real overhangScale = 20.0f;
			const real distanceScale = 1000.0f;

			SpawnSearchResult result = {};
			real bestScore = FLT_MAX;
			real fallbackScore = FLT_MAX;
			glm::vec3 fallbackPos(0.0f, density.SampleColumn(0.0f, 0.0f).height, 0.0f);

			auto IsWet = [](const ColumnSample& column, real y)
			{
				return column.lakeID != 0 && column.lakeT < 1.15f && y < column.waterLevel + 1.0f;
			};

			// Rings of samples around the origin, spaced further apart further out. Where each ring starts is derived
			// from the seed (so ties aren't always broken towards the same direction).
			const real startAngle = HashToUnit(DeriveSeed(density.Seed(), SEED_SPAWN)) * 6.28318531f;
			u32 ring = 0;
			for (real radius = 0.0f; radius <= maxRadius; ++ring)
			{
				const real distanceScore = radius / distanceScale;
				if (distanceScore >= bestScore)
				{
					// Nothing further out can beat the best found
					break;
				}

				const real step = 40.0f + radius * 0.02f;
				const u32 count = radius == 0.0f ? 1u : (u32)glm::ceil(6.28318531f * radius / step);
				for (u32 i = 0; i < count; ++i)
				{
					const real angle = startAngle + 6.28318531f * ((real)i + 0.5f * (real)(ring & 1)) / (real)count;
					const real x = std::cos(angle) * radius;
					const real z = std::sin(angle) * radius;
					const ColumnSample column = density.SampleColumn(x, z);
					++result.samples;
					const real h = column.height;
					if (IsWet(column, h))
					{
						continue;
					}

					const real heightScore = h >= idealHeight ? (h - idealHeight) / heightAboveScale : (idealHeight - h) / heightBelowScale;
					if (heightScore + distanceScore < fallbackScore)
					{
						fallbackScore = heightScore + distanceScore;
						fallbackPos = glm::vec3(x, h, z);
					}

					// (Not on beaches: land near the sea is always above sea level)
					if (h < minHeight || h > maxHeight || (column.oceanWeight > 0.0f && h < seaLevel + 2.5f) ||
						column.massifWeight > 0.05f || column.archRadius > 0.0f || heightScore + distanceScore >= bestScore)
					{
						continue;
					}

					// Slope of the heightfield around the point
					const real e = 8.0f;
					const real slopeX = Abs(density.SampleColumn(x + e, z).height - density.SampleColumn(x - e, z).height) / (2.0f * e);
					const real slopeZ = Abs(density.SampleColumn(x, z + e).height - density.SampleColumn(x, z - e).height) / (2.0f * e);
					result.samples += 4;
					const real slope = Max(slopeX, slopeZ);
					// (Smoother ground is preferred: overhangs make the surface rougher than the heightfield)
					const real score = heightScore + slope / slopeScale + column.overhangAmplitude / overhangScale + distanceScore;
					if (slope > maxSlope || score >= bestScore)
					{
						continue;
					}

					// The actual (3D) surface must be open to the sky, flat too (overhangs can make ledges on flat
					// heightfields) & solid for a way below (not the roof of a cave or a cave entrance)
					const real yMin = h - column.overhangAmplitude * 1.6f - 12.0f;
					const real yMax = h + density.MaxExtentAbove() + 10.0f;
					real y;
					if (!density.FindTopSurface(x, z, yMin, yMax, column, {}, y) ||
						IsWet(column, y) ||
						density.Sample(glm::vec3(x, y - 3.0f, z), column) > 0.0f ||
						density.Sample(glm::vec3(x, y - 10.0f, z), column) > 0.0f)
					{
						continue;
					}
					const real s = 3.0f;
					bool bFlat = true;
					for (const glm::vec2& offset : { glm::vec2(s, 0.0f), glm::vec2(-s, 0.0f), glm::vec2(0.0f, s), glm::vec2(0.0f, -s) })
					{
						const ColumnSample neighbour = density.SampleColumn(x + offset.x, z + offset.y);
						real neighbourY;
						if (!density.FindTopSurface(x + offset.x, z + offset.y, yMin, yMax, neighbour, {}, neighbourY) || Abs(neighbourY - y) > maxSlope * 1.5f * s)
						{
							bFlat = false;
							break;
						}
					}
					if (!bFlat)
					{
						continue;
					}

					bestScore = score;
					result.pos = glm::vec3(x, y, z);
					result.bIdeal = true;
				}

				radius += step;
			}

			if (!result.bIdeal)
			{
				result.pos = fallbackPos;
			}
			return result;
		}

		void ChunkMeshData::Clear()
		{
			positions.clear();
			normals.clear();
			colours.clear();
			weights.clear();
			indices.clear();
			collisionIndexCount = ~0u;
			waterPositions.clear();
			waterIndices.clear();
		}

		// -----------------------------------------------------------------------------------------
		// Packing (compact form for disk caching)
		// -----------------------------------------------------------------------------------------

		class ByteWriter
		{
		public:
			explicit ByteWriter(std::vector<u8>& out) : m_Out(out) {}

			void Bytes(const void* data, size_t size)
			{
				if (size == 0)
				{
					return;
				}
				const size_t offset = m_Out.size();
				m_Out.resize(offset + size);
				memcpy(m_Out.data() + offset, data, size);
			}

			template<typename T>
			void Write(const T& value)
			{
				Bytes(&value, sizeof(T));
			}

		private:
			std::vector<u8>& m_Out;
		};

		class ByteReader
		{
		public:
			ByteReader(const u8* data, size_t size) : m_Data(data), m_Size(size) {}

			bool Bytes(void* out, size_t size)
			{
				if (m_Offset + size > m_Size)
				{
					m_bFailed = true;
					return false;
				}
				if (size > 0)
				{
					memcpy(out, m_Data + m_Offset, size);
				}
				m_Offset += size;
				return true;
			}

			template<typename T>
			bool Read(T& outValue)
			{
				return Bytes(&outValue, sizeof(T));
			}

			const u8* Current() const { return m_Data + m_Offset; }
			bool Skip(size_t size)
			{
				if (m_Offset + size > m_Size)
				{
					m_bFailed = true;
					return false;
				}
				m_Offset += size;
				return true;
			}
			bool Failed() const { return m_bFailed; }
			bool AtEnd() const { return m_Offset == m_Size; }

		private:
			const u8* m_Data;
			size_t m_Size;
			size_t m_Offset = 0;
			bool m_bFailed = false;
		};

		static inline u8 PackUnorm8(real v)
		{
			return (u8)(Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
		}

		static inline i16 PackSnorm16(real v)
		{
			return (i16)(Clamp(v, -1.0f, 1.0f) * 32767.0f + (v >= 0.0f ? 0.5f : -0.5f));
		}

		// Octahedral normal encoding
		static void PackNormal(const glm::vec3& n, i16& outX, i16& outY)
		{
			const real invL1 = 1.0f / (Abs(n.x) + Abs(n.y) + Abs(n.z) + 1e-20f);
			real x = n.x * invL1;
			real y = n.y * invL1;
			if (n.z < 0.0f)
			{
				const real ox = (1.0f - Abs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
				const real oy = (1.0f - Abs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
				x = ox;
				y = oy;
			}
			outX = PackSnorm16(x);
			outY = PackSnorm16(y);
		}

		static glm::vec3 UnpackNormal(i16 px, i16 py)
		{
			const real x = (real)px / 32767.0f;
			const real y = (real)py / 32767.0f;
			glm::vec3 n(x, y, 1.0f - Abs(x) - Abs(y));
			if (n.z < 0.0f)
			{
				const real ox = (1.0f - Abs(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f);
				const real oy = (1.0f - Abs(n.x)) * (n.y >= 0.0f ? 1.0f : -1.0f);
				n.x = ox;
				n.y = oy;
			}
			return glm::normalize(n);
		}

		static void WriteIndices(ByteWriter& writer, const std::vector<u32>& indices, u32 vertexCount)
		{
			if (vertexCount <= 0xFFFF)
			{
				for (u32 index : indices)
				{
					writer.Write((u16)index);
				}
			}
			else
			{
				writer.Bytes(indices.data(), indices.size() * sizeof(u32));
			}
		}

		static bool ReadIndices(ByteReader& reader, std::vector<u32>& outIndices, u32 indexCount, u32 vertexCount)
		{
			outIndices.resize(indexCount);
			if (vertexCount <= 0xFFFF)
			{
				for (u32 i = 0; i < indexCount; ++i)
				{
					u16 index;
					if (!reader.Read(index))
					{
						return false;
					}
					outIndices[i] = index;
				}
			}
			else if (!reader.Bytes(outIndices.data(), (size_t)indexCount * sizeof(u32)))
			{
				return false;
			}

			for (u32 index : outIndices)
			{
				if (index >= vertexCount)
				{
					return false;
				}
			}
			return true;
		}

		void PackChunkMesh(const ChunkMeshData& mesh, PackedChunk& outPacked)
		{
			outPacked.clear();
			const u32 vertexCount = (u32)mesh.positions.size();
			const u32 waterVertexCount = (u32)mesh.waterPositions.size();
			outPacked.reserve(16 + vertexCount * 24 + mesh.indices.size() * 2 + waterVertexCount * 12 + mesh.waterIndices.size() * 2);

			ByteWriter writer(outPacked);
			writer.Write(vertexCount);
			writer.Write((u32)mesh.indices.size());
			writer.Write(waterVertexCount);
			writer.Write((u32)mesh.waterIndices.size());
			writer.Write(mesh.collisionIndexCount);

			// Positions stay full precision so vertices shared with neighboring chunks remain bit-identical
			writer.Bytes(mesh.positions.data(), vertexCount * sizeof(glm::vec3));
			for (u32 i = 0; i < vertexCount; ++i)
			{
				i16 nx, ny;
				PackNormal(mesh.normals[i], nx, ny);
				writer.Write(nx);
				writer.Write(ny);
				const glm::vec4& colour = mesh.colours[i];
				const glm::vec4& weights = mesh.weights[i];
				const u8 packed[8] =
				{
					PackUnorm8(colour.x), PackUnorm8(colour.y), PackUnorm8(colour.z), PackUnorm8(colour.w),
					PackUnorm8(weights.x), PackUnorm8(weights.y), PackUnorm8(weights.z), PackUnorm8(weights.w),
				};
				writer.Bytes(packed, sizeof(packed));
			}
			WriteIndices(writer, mesh.indices, vertexCount);

			writer.Bytes(mesh.waterPositions.data(), waterVertexCount * sizeof(glm::vec3));
			WriteIndices(writer, mesh.waterIndices, waterVertexCount);
		}

		bool UnpackChunkMesh(const u8* data, size_t size, ChunkMeshData& outMesh)
		{
			outMesh.Clear();
			ByteReader reader(data, size);

			u32 vertexCount, indexCount, waterVertexCount, waterIndexCount;
			if (!reader.Read(vertexCount) || !reader.Read(indexCount) || !reader.Read(waterVertexCount) || !reader.Read(waterIndexCount))
			{
				return false;
			}
			if (!reader.Read(outMesh.collisionIndexCount))
			{
				return false;
			}

			// Sanity check counts against the remaining size before allocating
			if ((u64)vertexCount * 24 + (u64)waterVertexCount * 12 > (u64)size)
			{
				return false;
			}

			outMesh.positions.resize(vertexCount);
			if (!reader.Bytes(outMesh.positions.data(), vertexCount * sizeof(glm::vec3)))
			{
				return false;
			}
			outMesh.normals.resize(vertexCount);
			outMesh.colours.resize(vertexCount);
			outMesh.weights.resize(vertexCount);
			for (u32 i = 0; i < vertexCount; ++i)
			{
				i16 nx, ny;
				u8 packed[8];
				if (!reader.Read(nx) || !reader.Read(ny) || !reader.Bytes(packed, sizeof(packed)))
				{
					return false;
				}
				outMesh.normals[i] = UnpackNormal(nx, ny);
				outMesh.colours[i] = glm::vec4(packed[0], packed[1], packed[2], packed[3]) / 255.0f;
				outMesh.weights[i] = glm::vec4(packed[4], packed[5], packed[6], packed[7]) / 255.0f;
			}
			if (!ReadIndices(reader, outMesh.indices, indexCount, vertexCount))
			{
				return false;
			}

			outMesh.waterPositions.resize(waterVertexCount);
			if (!reader.Bytes(outMesh.waterPositions.data(), waterVertexCount * sizeof(glm::vec3)))
			{
				return false;
			}
			if (!ReadIndices(reader, outMesh.waterIndices, waterIndexCount, waterVertexCount))
			{
				return false;
			}

			return !reader.Failed() && reader.AtEnd();
		}

		// -----------------------------------------------------------------------------------------
		// Edits
		// -----------------------------------------------------------------------------------------

		size_t EditStore::CellHash::operator()(const glm::ivec3& cell) const
		{
			return (size_t)HashCoords(cell.x, cell.y, cell.z, 0x2c1b3c6du);
		}

		static void HashEdit(u64& hash, const Edit& edit)
		{
			Hasher hasher;
			hasher.value = hash;
			hasher.Add(edit.center.x);
			hasher.Add(edit.center.y);
			hasher.Add(edit.center.z);
			hasher.Add(edit.radius);
			hasher.Add((u8)edit.type);
			hasher.Add((u8)edit.material);
			if (IsPlaneEdit(edit.type))
			{
				hasher.Add(edit.normal.x);
				hasher.Add(edit.normal.y);
				hasher.Add(edit.normal.z);
				hasher.Add(edit.height);
			}
			hash = hasher.value;
		}

		// Plane edits are full strength within this fraction of their radius, & fade out beyond it
		static const real PLANE_EDIT_INNER_RADIUS = 0.45f;
		// ... & fade out over the last part of their height
		static const real PLANE_EDIT_INNER_HEIGHT = 0.7f;

		real PlaneEditWeight(const Edit& edit, const glm::vec3& offset)
		{
			const real axial = offset.x * edit.normal.x + offset.y * edit.normal.y + offset.z * edit.normal.z;
			const real absAxial = Abs(axial);
			if (absAxial >= edit.height)
			{
				return 0.0f;
			}
			const real radialSq = Max(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z - axial * axial, 0.0f);
			if (radialSq >= edit.radius * edit.radius)
			{
				return 0.0f;
			}
			const real radial = std::sqrt(radialSq);
			return (1.0f - Smoothstep(edit.radius * PLANE_EDIT_INNER_RADIUS, edit.radius, radial)) *
				(1.0f - Smoothstep(edit.height * PLANE_EDIT_INNER_HEIGHT, edit.height, absAxial));
		}

		void GetEditBounds(const Edit& edit, glm::vec3& outMin, glm::vec3& outMax)
		{
			if (!IsPlaneEdit(edit.type))
			{
				outMin = edit.center - glm::vec3(edit.radius);
				outMax = edit.center + glm::vec3(edit.radius);
				return;
			}
			// A cylinder: its axis' extent plus its disc's along each world axis
			const glm::vec3 n = edit.normal;
			const glm::vec3 discExtent = edit.radius * glm::sqrt(glm::max(glm::vec3(1.0f) - n * n, glm::vec3(0.0f)));
			const glm::vec3 extent = glm::abs(n) * edit.height + discExtent;
			outMin = edit.center - extent;
			outMax = edit.center + extent;
		}

		Edit MakePlaneEdit(EditType type, const glm::vec3& center, const glm::vec3& normal, real radius, real height, Material material)
		{
			Edit edit = {};
			edit.type = type;
			edit.center = center;
			const real normalLength = glm::length(normal);
			edit.normal = normalLength > 1e-6f ? normal / normalLength : glm::vec3(0.0f, 1.0f, 0.0f);
			edit.radius = Max(radius, 0.05f);
			// Enough to reach the surface of a 45 degree slope from the rim
			edit.height = height > 0.0f ? height : edit.radius * 1.5f + 1.0f;
			edit.material = material;
			return edit;
		}

		// Whether edit (a plane edit) is the same brush stroke as other, only further along: the same disc, with its plane
		// moved further in the direction it moves the surface, & reaching at least as far on the side it works on
		static bool ContinuesPlaneEdit(const Edit& edit, const Edit& other)
		{
			if (edit.type != other.type || edit.material != other.material || edit.radius != other.radius || edit.normal != other.normal ||
				edit.type == EditType::FLATTEN)
			{
				return false;
			}
			const glm::vec3 offset = edit.center - other.center;
			const real axial = glm::dot(offset, edit.normal);
			const real lateral = glm::length(offset - edit.normal * axial);
			if (lateral > 1e-3f)
			{
				return false;
			}
			if (edit.type == EditType::LOWER)
			{
				// Deeper, & reaching at least as high
				return axial <= 1e-4f && axial + edit.height >= other.height - 1e-4f;
			}
			// Higher, & reaching at least as low
			return axial >= -1e-4f && axial - edit.height <= -other.height + 1e-4f;
		}

		void EditStore::Add(const Edit& edit)
		{
			// A dig/fill which contains the previous edit of the same kind entirely (e.g. a hole growing while the player
			// keeps digging) replaces it. The result is identical (the larger sphere dominates wherever the smaller one
			// has any effect), but keeps the edit count down.
			// Plane edits continuing the last one (a brush stroke growing deeper or higher) replace it too. That isn't
			// identical to applying both (their blended rims would add up), but is what the stroke means.
			u32 index = (u32)m_Edits.size();
			bool bReplacesLast = false;
			if (!m_Edits.empty() && edit.type != EditType::REMOVE_PLANT)
			{
				const Edit& last = m_Edits.back();
				if (IsPlaneEdit(edit.type))
				{
					bReplacesLast = ContinuesPlaneEdit(edit, last);
				}
				else
				{
					bReplacesLast = last.type == edit.type &&
						last.material == edit.material &&
						glm::distance(last.center, edit.center) + last.radius <= edit.radius + 1e-4f;
				}
			}

			if (bReplacesLast)
			{
				index = (u32)m_Edits.size() - 1;
				m_Edits.back() = edit;
				m_Hash = m_HashBeforeLast;
			}
			else
			{
				m_Edits.push_back(edit);
				m_HashBeforeLast = m_Hash;
			}
			HashEdit(m_Hash, edit);

			glm::vec3 editMin, editMax;
			GetEditBounds(edit, editMin, editMax);
			m_BoundsMin = glm::min(m_BoundsMin, editMin);
			m_BoundsMax = glm::max(m_BoundsMax, editMax);

			const glm::ivec3 cellMin = glm::ivec3(glm::floor(editMin / CELL_SIZE));
			const glm::ivec3 cellMax = glm::ivec3(glm::floor(editMax / CELL_SIZE));
			for (i32 x = cellMin.x; x <= cellMax.x; ++x)
			{
				for (i32 y = cellMin.y; y <= cellMax.y; ++y)
				{
					for (i32 z = cellMin.z; z <= cellMax.z; ++z)
					{
						std::vector<u32>& cell = m_Cells[glm::ivec3(x, y, z)];
						if (cell.empty() || cell.back() != index)
						{
							cell.push_back(index);
						}
					}
				}
			}
		}

		void EditStore::Clear()
		{
			m_Edits.clear();
			m_Cells.clear();
			m_BoundsMin = glm::vec3(1e30f);
			m_BoundsMax = glm::vec3(-1e30f);
			m_Hash = 0;
			m_HashBeforeLast = 0;
		}

		static bool BoxesOverlap(const glm::vec3& aMin, const glm::vec3& aMax, const glm::vec3& bMin, const glm::vec3& bMax)
		{
			return aMin.x <= bMax.x && aMax.x >= bMin.x &&
				aMin.y <= bMax.y && aMax.y >= bMin.y &&
				aMin.z <= bMax.z && aMax.z >= bMin.z;
		}

		void EditStore::Gather(const glm::vec3& boxMin, const glm::vec3& boxMax, std::vector<Edit>& outEdits) const
		{
			if (m_Edits.empty() || !BoxesOverlap(boxMin, boxMax, m_BoundsMin, m_BoundsMax))
			{
				return;
			}

			std::vector<u32> indices;
			const glm::ivec3 cellMin = glm::ivec3(glm::floor(glm::max(boxMin, m_BoundsMin) / CELL_SIZE));
			const glm::ivec3 cellMax = glm::ivec3(glm::floor(glm::min(boxMax, m_BoundsMax) / CELL_SIZE));
			const u64 cellCount = (u64)(cellMax.x - cellMin.x + 1) * (u64)(cellMax.y - cellMin.y + 1) * (u64)(cellMax.z - cellMin.z + 1);
			if (cellCount > (u64)m_Cells.size())
			{
				for (const auto& pair : m_Cells)
				{
					const glm::ivec3& cell = pair.first;
					if (cell.x >= cellMin.x && cell.x <= cellMax.x && cell.y >= cellMin.y && cell.y <= cellMax.y && cell.z >= cellMin.z && cell.z <= cellMax.z)
					{
						indices.insert(indices.end(), pair.second.begin(), pair.second.end());
					}
				}
			}
			else
			{
				for (i32 x = cellMin.x; x <= cellMax.x; ++x)
				{
					for (i32 y = cellMin.y; y <= cellMax.y; ++y)
					{
						for (i32 z = cellMin.z; z <= cellMax.z; ++z)
						{
							auto iter = m_Cells.find(glm::ivec3(x, y, z));
							if (iter != m_Cells.end())
							{
								indices.insert(indices.end(), iter->second.begin(), iter->second.end());
							}
						}
					}
				}
			}

			// Edits must be applied in order
			std::sort(indices.begin(), indices.end());
			indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
			for (u32 index : indices)
			{
				const Edit& edit = m_Edits[index];
				glm::vec3 editMin, editMax;
				GetEditBounds(edit, editMin, editMax);
				if (BoxesOverlap(boxMin, boxMax, editMin, editMax))
				{
					outEdits.push_back(edit);
				}
			}
		}

		bool EditStore::Overlaps(const glm::vec3& boxMin, const glm::vec3& boxMax) const
		{
			if (m_Edits.empty() || !BoxesOverlap(boxMin, boxMax, m_BoundsMin, m_BoundsMax))
			{
				return false;
			}
			std::vector<Edit> edits;
			Gather(boxMin, boxMax, edits);
			return !edits.empty();
		}

		// Version 1: "FTED", version, seed, count, then the edits
		static const u32 EDITS_FILE_MAGIC = 0x44455446; // "FTED"
		static const u32 EDITS_FILE_VERSION = 1;
		// Version 2: a binary save container (see SaveFile.hpp) holding a header & the edits (byte shuffled, so they compress
		// well if that's ever worth it)
		static const u32 EDITS_FILE_TYPE = save::FourCC('T', 'E', 'D', 'T');
		static const u32 EDITS_CONTAINER_FILE_VERSION = 2;
		static const u32 EDITS_SECTION_HEADER = save::FourCC('E', 'H', 'D', 'R');
		static const u32 EDITS_SECTION_LIST = save::FourCC('E', 'D', 'I', 'T');
		// center, radius, type, material
		static const u32 EDIT_RECORD_SIZE_V2 = sizeof(glm::vec3) + sizeof(real) + 2;
		// ... then (appended, so older readers skip them) the plane edit fields: normal, height
		static const u32 EDIT_RECORD_SIZE = EDIT_RECORD_SIZE_V2 + sizeof(glm::vec3) + sizeof(real);

		bool WriteEdits(const std::string& filePath, i32 seed, const std::vector<Edit>& edits)
		{
			std::vector<u8> records(edits.size() * EDIT_RECORD_SIZE);
			u8* record = records.data();
			for (const Edit& edit : edits)
			{
				memcpy(record, &edit.center, sizeof(glm::vec3));
				memcpy(record + 12, &edit.radius, sizeof(real));
				record[16] = (u8)edit.type;
				record[17] = (u8)edit.material;
				memcpy(record + 18, &edit.normal, sizeof(glm::vec3));
				memcpy(record + 30, &edit.height, sizeof(real));
				record += EDIT_RECORD_SIZE;
			}
			// Grouping each byte of the floats together makes them much more compressible
			std::vector<u8> shuffled(records.size());
			save::ShuffleBytes(records.data(), edits.size(), EDIT_RECORD_SIZE, shuffled.data());

			save::Writer header;
			header.Write(seed);
			header.Write((u32)edits.size());
			header.Write(EDIT_RECORD_SIZE);

			save::FileWriter writer(EDITS_FILE_TYPE, EDITS_CONTAINER_FILE_VERSION);
			writer.AddSection(EDITS_SECTION_HEADER, header.bytes, 0, false);
			// Not compressed: zlib halves the size (shuffled), but even 100k edits are only 1.7 MB, & compressing them
			// took ~5x longer to save (100 vs 17 ms, on the main thread) & ~8x longer to load (22 vs 3 ms). The reader
			// handles compressed sections, so this can change without a new version.
			writer.AddSection(EDITS_SECTION_LIST, shuffled, 0, false);
			// Written atomically, keeping the previous file as a backup
			return writer.Save(filePath, true);
		}

		static bool ReadWholeFile(const std::string& filePath, std::vector<u8>& outBytes)
		{
			std::ifstream file(filePath, std::ios::binary | std::ios::ate);
			if (!file)
			{
				return false;
			}
			const std::streamsize size = file.tellg();
			if (size <= 0)
			{
				return false;
			}
			outBytes.resize((size_t)size);
			file.seekg(0);
			return (bool)file.read((char*)outBytes.data(), size);
		}

		static bool ReadEditsV1(const std::vector<u8>& bytes, i32 seed, std::vector<Edit>& outEdits)
		{
			ByteReader reader(bytes.data(), bytes.size());
			u32 magic, version, count;
			i32 fileSeed;
			if (!reader.Read(magic) || !reader.Read(version) || !reader.Read(fileSeed) || !reader.Read(count) ||
				magic != EDITS_FILE_MAGIC || version != EDITS_FILE_VERSION || fileSeed != seed ||
				(u64)count * EDIT_RECORD_SIZE_V2 > (u64)(bytes.size() - 16))
			{
				return false;
			}

			outEdits.reserve(count);
			for (u32 i = 0; i < count; ++i)
			{
				Edit edit = {};
				u8 type, material;
				if (!reader.Read(edit.center.x) || !reader.Read(edit.center.y) || !reader.Read(edit.center.z) || !reader.Read(edit.radius) ||
					!reader.Read(type) || !reader.Read(material))
				{
					outEdits.clear();
					return false;
				}
				// (Version 1 had no plane edits)
				edit.type = type <= (u8)EditType::REMOVE_PLANT ? (EditType)type : EditType::DIG;
				edit.material = material < (u8)Material::_COUNT ? (Material)material : Material::SOIL;
				outEdits.push_back(edit);
			}
			return true;
		}

		static bool ReadEditsContainer(const save::FileReader& file, i32 seed, std::vector<Edit>& outEdits)
		{
			std::vector<u8> headerBytes, shuffled;
			if (!file.GetSection(EDITS_SECTION_HEADER, headerBytes) || !file.GetSection(EDITS_SECTION_LIST, shuffled))
			{
				return false;
			}
			save::Reader header(headerBytes.data(), headerBytes.size());
			i32 fileSeed;
			u32 count, recordSize;
			// (Newer versions may append fields to each record, which are ignored)
			if (!header.Read(fileSeed) || !header.Read(count) || !header.Read(recordSize) || fileSeed != seed ||
				recordSize < EDIT_RECORD_SIZE_V2 || recordSize > 1024 || (u64)count * recordSize != (u64)shuffled.size())
			{
				return false;
			}

			std::vector<u8> records(shuffled.size());
			save::UnshuffleBytes(shuffled.data(), count, recordSize, records.data());
			outEdits.resize(count);
			const u8* record = records.data();
			for (Edit& edit : outEdits)
			{
				memcpy(&edit.center, record, sizeof(glm::vec3));
				memcpy(&edit.radius, record + 12, sizeof(real));
				edit.type = record[16] < (u8)EditType::_COUNT ? (EditType)record[16] : EditType::DIG;
				edit.material = record[17] < (u8)Material::_COUNT ? (Material)record[17] : Material::SOIL;
				if (recordSize >= EDIT_RECORD_SIZE)
				{
					memcpy(&edit.normal, record + 18, sizeof(glm::vec3));
					memcpy(&edit.height, record + 30, sizeof(real));
				}
				if (IsPlaneEdit(edit.type))
				{
					// (Corrupt values would make the density NaN)
					const real normalLength = glm::length(edit.normal);
					if (!(normalLength > 0.5f && normalLength < 1.5f) || !(edit.height >= 0.0f && edit.height < 1000.0f) || !(edit.radius >= 0.0f && edit.radius < 1000.0f))
					{
						edit.type = EditType::REMOVE_PLANT;
						edit.radius = 0.0f;
					}
				}
				record += recordSize;
			}
			return true;
		}

		static bool ReadEditsFile(const std::string& filePath, i32 seed, std::vector<Edit>& outEdits, bool& bOutCorrupt)
		{
			outEdits.clear();
			bOutCorrupt = false;

			std::vector<u8> bytes;
			if (!save::ReadFileBytes(filePath, bytes))
			{
				return false;
			}

			save::FileReader file;
			const save::LoadResult result = file.Parse(bytes.data(), bytes.size(), EDITS_FILE_TYPE);
			bool bRead;
			if (result == save::LoadResult::NOT_CONTAINER)
			{
				bRead = ReadEditsV1(bytes, seed, outEdits);
			}
			else
			{
				bRead = result == save::LoadResult::OK && ReadEditsContainer(file, seed, outEdits);
				bOutCorrupt = result != save::LoadResult::OK || (!bRead && file.HasSection(EDITS_SECTION_LIST));
			}
			if (!bRead)
			{
				outEdits.clear();
			}
			return bRead;
		}

		bool ReadEdits(const std::string& filePath, i32 seed, std::vector<Edit>& outEdits)
		{
			bool bCorrupt;
			if (ReadEditsFile(filePath, seed, outEdits, bCorrupt))
			{
				return true;
			}
			// A missing or damaged file falls back to the backup kept by the last save
			bool bBackupCorrupt;
			if ((bCorrupt || !FileExists(filePath)) && ReadEditsFile(filePath + ".bak", seed, outEdits, bBackupCorrupt))
			{
				PrintWarn("Terrain edits %s were %s, loaded %u from its backup\n", filePath.c_str(), bCorrupt ? "damaged" : "missing", (u32)outEdits.size());
				return true;
			}
			if (bCorrupt)
			{
				PrintError("Terrain edits %s are damaged & couldn't be loaded\n", filePath.c_str());
			}
			return false;
		}

		// -----------------------------------------------------------------------------------------
		// Worker pool
		// -----------------------------------------------------------------------------------------

		TerrainWorkerPool::~TerrainWorkerPool()
		{
			Stop();
		}

		void TerrainWorkerPool::Start(u32 threadCount)
		{
			Stop();

			m_bRunning = true;
			threadCount = glm::max(threadCount, 1u);
			for (u32 i = 0; i < threadCount; ++i)
			{
				m_Threads.emplace_back(&TerrainWorkerPool::ThreadMain, this);
			}
		}

		void TerrainWorkerPool::Stop()
		{
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_bRunning = false;
				m_Queue.clear();
			}
			m_CV.notify_all();

			for (std::thread& thread : m_Threads)
			{
				thread.join();
			}
			m_Threads.clear();

			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Results.clear();
		}

		void TerrainWorkerPool::SortQueue()
		{
			// Highest priority (lowest value) at the back
			std::sort(m_Queue.begin(), m_Queue.end(), [](const ChunkJob& a, const ChunkJob& b) { return a.priority > b.priority; });
		}

		void TerrainWorkerPool::SetJobs(std::vector<ChunkJob>&& jobs, const std::unordered_map<u64, real>& retainedJobPriorities /* = {} */)
		{
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_Queue.erase(std::remove_if(m_Queue.begin(), m_Queue.end(), [&retainedJobPriorities](ChunkJob& job)
				{
					if (job.bEdit)
					{
						return false;
					}
					auto iter = retainedJobPriorities.find(job.jobID);
					if (iter == retainedJobPriorities.end())
					{
						return true;
					}
					job.priority = iter->second;
					return false;
				}), m_Queue.end());
				for (ChunkJob& job : jobs)
				{
					m_Queue.emplace_back(std::move(job));
				}
				SortQueue();
			}
			m_CV.notify_all();
		}

		void TerrainWorkerPool::AddJobs(std::vector<ChunkJob>&& jobs)
		{
			if (jobs.empty())
			{
				return;
			}

			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				for (ChunkJob& job : jobs)
				{
					m_Queue.emplace_back(std::move(job));
				}
				SortQueue();
			}
			m_CV.notify_all();
		}

		void TerrainWorkerPool::TakeResults(std::vector<ChunkResult>& outResults)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			for (ChunkResult& result : m_Results)
			{
				outResults.emplace_back(std::move(result));
			}
			m_Results.clear();
		}

		u32 TerrainWorkerPool::QueuedJobCount()
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			return (u32)m_Queue.size();
		}

		void TerrainWorkerPool::ThreadMain()
		{
			// Streaming happens in the background over many frames, so per-frame work (the main thread & job system)
			// takes precedence when both need the CPU
			Platform::SetCurrentThreadBackgroundPriority();

			while (true)
			{
				ChunkJob job;
				{
					std::unique_lock<std::mutex> lock(m_Mutex);
					m_CV.wait(lock, [this] { return !m_bRunning || !m_Queue.empty(); });
					if (!m_bRunning)
					{
						return;
					}
					job = std::move(m_Queue.back());
					m_Queue.pop_back();
				}

				ChunkResult result = {};
				result.key = job.key;
				result.mask = job.mask;
				result.jobID = job.jobID;

				const auto start = std::chrono::steady_clock::now();
				EditList editList;
				editList.edits = job.edits.data();
				editList.count = (u32)job.edits.size();
				GenerateChunkMesh(*job.density, job.key, job.mask, editList, result.mesh);
				std::shared_ptr<PackedChunk> packed = std::make_shared<PackedChunk>();
				PackChunkMesh(result.mesh, *packed);
				result.packed = std::move(packed);
				const auto end = std::chrono::steady_clock::now();
				result.generationTime = std::chrono::duration<real, std::milli>(end - start).count();

				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					m_Results.emplace_back(std::move(result));
				}
			}
		}

		// -----------------------------------------------------------------------------------------
		// Streamer
		// -----------------------------------------------------------------------------------------

		static real DistanceToBox(const glm::vec3& point, const glm::vec3& boxMin, const glm::vec3& boxMax)
		{
			const glm::vec3 delta = glm::max(glm::max(boxMin - point, point - boxMax), glm::vec3(0.0f));
			return glm::length(delta);
		}

		struct EditBoundsCellHash
		{
			size_t operator()(const glm::ivec3& cell) const
			{
				return (size_t)HashCoords(cell.x, cell.y, cell.z, 0x5f356495u);
			}
		};

		static const u32 SNAPSHOT_FILE_MAGIC = 0x4E535446; // "FTSN"
		static const u32 SNAPSHOT_FILE_VERSION = 1;

		TerrainStreamer::TerrainStreamer()
		{
		}

		TerrainStreamer::~TerrainStreamer()
		{
			Stop();
		}

		void TerrainStreamer::Start(const WorldParams& params, u32 threadCount, const std::string& snapshotPath /* = "" */)
		{
			m_Pool.Start(threadCount);
			SetParams(params);

			m_SnapshotPath = snapshotPath;
			// Show the chunks saved last session immediately (if nothing has changed since)
			LoadSnapshotAsync();
		}

		void TerrainStreamer::LoadSnapshotAsync()
		{
			if (m_SnapshotPath.empty())
			{
				return;
			}

			if (m_SnapshotFuture.valid())
			{
				// Stops as soon as it can
				*m_SnapshotCancelled = true;
				m_DiscardedSnapshotFutures.emplace_back(std::move(m_SnapshotFuture));
			}

			const std::string path = m_SnapshotPath;
			const u64 contentHash = m_Params.ContentHash();
			const u64 editsHash = m_Edits.Hash();
			m_bSnapshotReplacesDisplayed = !m_Displayed.empty();
			m_SnapshotCancelled = std::make_shared<std::atomic<bool>>(false);
			std::shared_ptr<std::atomic<bool>> cancelled = m_SnapshotCancelled;
			m_SnapshotFuture = std::async(std::launch::async, [path, contentHash, editsHash, cancelled]()
			{
				return LoadSnapshot(path, contentHash, editsHash, cancelled);
			});
		}

		void TerrainStreamer::Stop()
		{
			CancelDesiredComputation();
			if (m_SnapshotFuture.valid())
			{
				m_SnapshotFuture.wait();
				m_SnapshotFuture = {};
			}
			m_DiscardedSnapshotFutures.clear();
			WaitForSnapshotWrite();
			m_Pool.Stop();
			m_Pending.clear();
			m_EditPending.clear();
			m_SwapGroups.clear();
			m_RetiringCount = 0;
			m_Displayed.clear();
			m_bBatchInFlight = false;
		}

		void TerrainStreamer::SetParams(const WorldParams& params)
		{
			CancelDesiredComputation();

			m_Params = params;
			m_Params.cellsPerChunkAxis = glm::clamp(m_Params.cellsPerChunkAxis, 4u, 96u);
			m_Params.maxLodLevel = glm::clamp(m_Params.maxLodLevel, 0u, 16u);
			m_Params.lodDistanceFactor = glm::max(m_Params.lodDistanceFactor, 1.8f);
			m_Density = std::make_shared<const TerrainDensity>(m_Params);
			m_Params = m_Density->Params();

			// Displayed chunks remain until all of their replacements are ready
			RetireAllDisplayed();
			m_SwapGroups.clear();
			m_RetiringCount = 0;
			m_Pending.clear();
			m_EditPending.clear();
			m_Pool.SetJobs({});
			m_ColumnBounds.clear();
			m_bBatchInFlight = false;
			m_bForceRefresh = true;
		}

		void TerrainStreamer::SetLodDistanceFactor(real lodDistanceFactor)
		{
			// LOD selection may be running in the background, reading the params
			CancelDesiredComputation();
			m_Params.lodDistanceFactor = glm::max(lodDistanceFactor, 1.8f);
			m_bForceRefresh = true;
		}

		void TerrainStreamer::GetChunkBounds(const ChunkKey& key, glm::vec3& outMin, glm::vec3& outMax) const
		{
			const real size = m_Params.ChunkSize((u32)key.level);
			outMin = glm::vec3(key.index) * size;
			outMax = outMin + size;
		}

		bool TerrainStreamer::IsRootInView(const glm::vec3& cameraPos, const glm::ivec3& rootIndex) const
		{
			const real size = m_Params.ChunkSize(m_Params.maxLodLevel);
			const glm::vec3 boxMin = glm::vec3(rootIndex) * size;
			return DistanceToBox(cameraPos, boxMin, boxMin + size) < m_Params.viewDistance;
		}

		bool TerrainStreamer::ShouldSplit(const glm::vec3& cameraPos, i32 level, const glm::ivec3& index) const
		{
			if (level <= 0)
			{
				return false;
			}
			const real size = m_Params.ChunkSize((u32)level);
			const glm::vec3 boxMin = glm::vec3(index) * size;
			return DistanceToBox(cameraPos, boxMin, boxMin + size) < m_Params.lodDistanceFactor * size;
		}

		i32 TerrainStreamer::LeafLevelAt(const glm::vec3& cameraPos, const glm::vec3& pos) const
		{
			i32 level = (i32)m_Params.maxLodLevel;
			glm::ivec3 index = glm::ivec3(glm::floor(pos / m_Params.ChunkSize((u32)level)));
			if (!IsRootInView(cameraPos, index))
			{
				return INT32_MAX;
			}

			while (ShouldSplit(cameraPos, level, index))
			{
				--level;
				index = glm::ivec3(glm::floor(pos / m_Params.ChunkSize((u32)level)));
			}
			return level;
		}

		bool TerrainStreamer::ColumnMayContainSurface(i32 level, const glm::ivec3& index)
		{
			const real size = m_Params.ChunkSize((u32)level);
			const real minY = (real)index.y * size;
			const real maxY = minY + size;

			// Edits can create surfaces anywhere
			for (size_t i = 0; i + 1 < m_EditBoundsForSelection.size(); i += 2)
			{
				const glm::vec3 boxMin = glm::vec3(index) * size;
				if (BoxesOverlap(boxMin, boxMin + size, m_EditBoundsForSelection[i], m_EditBoundsForSelection[i + 1]))
				{
					return true;
				}
			}

			const ChunkKey columnKey = { level, glm::ivec3(index.x, 0, index.z) };
			auto iter = m_ColumnBounds.find(columnKey);
			if (iter == m_ColumnBounds.end())
			{
				if (m_ColumnBounds.size() > 200000)
				{
					m_ColumnBounds.clear();
				}

				const i32 sampleCount = 5;
				real minHeight = 1e30f;
				real maxHeight = -1e30f;
				for (i32 x = 0; x < sampleCount; ++x)
				{
					for (i32 z = 0; z < sampleCount; ++z)
					{
						const real px = ((real)index.x + (real)x / (sampleCount - 1)) * size;
						const real pz = ((real)index.z + (real)z / (sampleCount - 1)) * size;
						const ColumnSample column = m_Density->SampleColumn(px, pz);
						minHeight = glm::min(minHeight, column.height);
						maxHeight = glm::max(maxHeight, column.height);
						if (column.lakeID != 0)
						{
							maxHeight = glm::max(maxHeight, column.waterLevel);
						}
					}
				}

				// Generous margin since peaks may fall between samples. Chunks which turn out to be empty are cheap to reject.
				const real margin = size * 0.5f;
				const glm::vec2 bounds(minHeight - m_Density->MaxExtentBelow() - margin, maxHeight + m_Density->MaxExtentAbove() + margin);
				iter = m_ColumnBounds.emplace(columnKey, bounds).first;
			}

			return maxY >= iter->second.x && minY <= iter->second.y;
		}

		void TerrainStreamer::CollectLeaves(const glm::vec3& cameraPos, i32 level, const glm::ivec3& index, std::vector<ChunkKey>& outLeaves)
		{
			const real size = m_Params.ChunkSize((u32)level);
			const glm::vec3 boxMin = glm::vec3(index) * size;
			if (DistanceToBox(cameraPos, boxMin, boxMin + size) >= m_Params.viewDistance)
			{
				return;
			}

			if (!ColumnMayContainSurface(level, index))
			{
				return;
			}

			if (ShouldSplit(cameraPos, level, index))
			{
				for (i32 c = 0; c < 8; ++c)
				{
					const glm::ivec3 child = index * 2 + glm::ivec3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
					CollectLeaves(cameraPos, level - 1, child, outLeaves);
				}
			}
			else
			{
				outLeaves.push_back(ChunkKey{ level, index });
			}
		}

		TransitionMask TerrainStreamer::ComputeTransitionMask(const glm::vec3& cameraPos, const ChunkKey& key) const
		{
			if (key.level == 0)
			{
				return 0;
			}

			const real size = m_Params.ChunkSize((u32)key.level);
			const glm::vec3 center = (glm::vec3(key.index) + 0.5f) * size;

			auto IsFiner = [&](const glm::ivec3& offset)
			{
				return LeafLevelAt(cameraPos, center + glm::vec3(offset) * size) < key.level;
			};

			TransitionMask mask = 0;
			for (u32 axis = 0; axis < 3; ++axis)
			{
				for (u32 side = 0; side <= 1; ++side)
				{
					glm::ivec3 offset(0);
					offset[axis] = side ? 1 : -1;
					if (IsFiner(offset))
					{
						mask |= FaceBit(axis, side);
					}
				}
			}

			for (u32 axis = 0; axis < 3; ++axis)
			{
				const u32 o0 = axis == 0 ? 1 : 0;
				const u32 o1 = axis == 2 ? 1 : 2;
				for (u32 s0 = 0; s0 <= 1; ++s0)
				{
					for (u32 s1 = 0; s1 <= 1; ++s1)
					{
						if ((mask & FaceBit(o0, s0)) || (mask & FaceBit(o1, s1)))
						{
							mask |= EdgeBit(axis, s0, s1);
							continue;
						}

						glm::ivec3 offset(0);
						offset[o0] = s0 ? 1 : -1;
						offset[o1] = s1 ? 1 : -1;
						if (IsFiner(offset))
						{
							mask |= EdgeBit(axis, s0, s1);
						}
					}
				}
			}

			return mask;
		}

		void TerrainStreamer::ComputeDesiredChunks(const glm::vec3& cameraPos, std::unordered_map<ChunkKey, TransitionMask, ChunkKeyHash>& outDesired)
		{
			outDesired.clear();

			const i32 rootLevel = (i32)m_Params.maxLodLevel;
			const real rootSize = m_Params.ChunkSize((u32)rootLevel);
			const glm::ivec3 rootMin = glm::ivec3(glm::floor((cameraPos - m_Params.viewDistance) / rootSize));
			const glm::ivec3 rootMax = glm::ivec3(glm::floor((cameraPos + m_Params.viewDistance) / rootSize));

			std::vector<ChunkKey> leaves;
			for (i32 x = rootMin.x; x <= rootMax.x; ++x)
			{
				for (i32 y = rootMin.y; y <= rootMax.y; ++y)
				{
					for (i32 z = rootMin.z; z <= rootMax.z; ++z)
					{
						const glm::ivec3 rootIndex(x, y, z);
						if (IsRootInView(cameraPos, rootIndex))
						{
							CollectLeaves(cameraPos, rootLevel, rootIndex, leaves);
						}
					}
				}
			}

			outDesired.reserve(leaves.size());
			for (const ChunkKey& leaf : leaves)
			{
				outDesired.emplace(leaf, ComputeTransitionMask(cameraPos, leaf));
			}
		}

		void TerrainStreamer::CancelDesiredComputation()
		{
			if (m_DesiredFuture.valid())
			{
				m_DesiredFuture.wait();
				m_DesiredFuture = {};
			}
		}

		ChunkJob TerrainStreamer::MakeJob(const ChunkKey& key, TransitionMask mask, real priority)
		{
			ChunkJob job = {};
			job.key = key;
			job.mask = mask;
			job.jobID = m_NextJobID++;
			job.density = m_Density;
			job.priority = priority;

			// Edits within the sampled region (which extends one cell beyond the chunk for gradients)
			glm::vec3 boxMin, boxMax;
			GetChunkBounds(key, boxMin, boxMax);
			const real margin = m_Params.CellSize((u32)key.level) * 2.0f + m_Density->EditBand();
			m_Edits.Gather(boxMin - margin, boxMax + margin, job.edits);
			return job;
		}

		real TerrainStreamer::ChunkPriority(const ChunkKey& key) const
		{
			glm::vec3 boxMin, boxMax;
			GetChunkBounds(key, boxMin, boxMax);

			// Distance to the camera's predicted path over the next couple of seconds, with points further along
			// the path counting for a little less (so chunks ahead are generated before those behind)
			const glm::vec3 travel = m_PriorityVelocity * 2.0f;
			const real travelDistance = glm::length(travel);
			real priority = DistanceToBox(m_PriorityCameraPos, boxMin, boxMax);
			if (travelDistance > m_Params.ChunkSize(0) * 0.25f)
			{
				const i32 stepCount = 4;
				for (i32 i = 1; i <= stepCount; ++i)
				{
					const real t = (real)i / (real)stepCount;
					priority = glm::min(priority, DistanceToBox(m_PriorityCameraPos + travel * t, boxMin, boxMax) + travelDistance * t * 0.5f);
				}
			}
			return priority;
		}

		void TerrainStreamer::UpdateVelocity(const glm::vec3& cameraPos)
		{
			const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
			if (m_bHasLastCameraPos)
			{
				const real dt = std::chrono::duration<real>(now - m_LastUpdateTime).count();
				if (dt > 1e-4f)
				{
					const glm::vec3 delta = cameraPos - m_LastCameraPos;
					if (glm::length(delta) > m_Params.ChunkSize(2) * 4.0f)
					{
						// Teleported
						m_SmoothedVelocity = glm::vec3(0.0f);
					}
					else
					{
						// Exponential smoothing with a ~0.4s time constant
						const real alpha = 1.0f - std::exp(-dt / 0.4f);
						m_SmoothedVelocity += (delta / dt - m_SmoothedVelocity) * alpha;
					}
				}
			}
			m_LastCameraPos = cameraPos;
			m_LastUpdateTime = now;
			m_bHasLastCameraPos = true;
		}

		void TerrainStreamer::StartBatch(DesiredChunks&& desiredChunks)
		{
			const glm::vec3 cameraPos = desiredChunks.cameraPos;
			const std::unordered_map<ChunkKey, TransitionMask, ChunkKeyHash>& desired = desiredChunks.chunks;
			m_DesiredCount = (u32)desired.size();
			m_PriorityCameraPos = cameraPos;
			m_PriorityVelocity = m_SmoothedVelocity;

			const bool bRetireAll = m_bRetireAllOnNextBatch;
			m_bRetireAllOnNextBatch = false;

			std::vector<ChunkKey> retiring;
			for (auto& pair : m_Displayed)
			{
				auto iter = desired.find(pair.first);
				if (bRetireAll || pair.second.bStale || iter == desired.end() || iter->second != pair.second.mask)
				{
					retiring.push_back(pair.first);
				}
			}

			// Chunks from the previous batch which are still wanted keep their (possibly finished) work
			std::unordered_map<ChunkKey, Pending, ChunkKeyHash> previousPending = std::move(m_Pending);
			m_Pending.clear();
			std::unordered_map<u64, real> retainedJobPriorities;
			std::vector<ChunkJob> jobs;
			for (auto& pair : desired)
			{
				auto displayedIter = m_Displayed.find(pair.first);
				if (!bRetireAll && displayedIter != m_Displayed.end() && !displayedIter->second.bStale && displayedIter->second.mask == pair.second)
				{
					continue;
				}

				const real priority = ChunkPriority(pair.first);

				auto previousIter = previousPending.find(pair.first);
				if (previousIter != previousPending.end() && previousIter->second.mask == pair.second)
				{
					if (!previousIter->second.bDone)
					{
						retainedJobPriorities.emplace(previousIter->second.jobID, priority);
					}
					m_Pending.emplace(pair.first, std::move(previousIter->second));
					continue;
				}

				ChunkJob job = MakeJob(pair.first, pair.second, priority);

				Pending pending = {};
				pending.mask = job.mask;
				pending.jobID = job.jobID;
				pending.bDone = false;
				m_Pending.emplace(job.key, std::move(pending));

				jobs.emplace_back(std::move(job));
			}

			m_Pool.SetJobs(std::move(jobs), retainedJobPriorities);

			BuildSwapGroups(retiring);

			m_BatchCameraPos = cameraPos;
			m_BatchDesired = desired;
			m_bBatchInFlight = !m_SwapGroups.empty();
			if (!m_bBatchInFlight)
			{
				VerifyBatchResult();
			}
		}

		void TerrainStreamer::BuildSwapGroups(const std::vector<ChunkKey>& retiring)
		{
			m_SwapGroups.clear();
			m_RetiringCount = (u32)retiring.size();

			// Union-find over new chunks [0, pendingCount) & retiring chunks [pendingCount, ...)
			std::vector<ChunkKey> keys;
			keys.reserve(m_Pending.size() + retiring.size());
			std::unordered_map<ChunkKey, u32, ChunkKeyHash> pendingIndices;
			std::unordered_map<ChunkKey, u32, ChunkKeyHash> retiringIndices;
			pendingIndices.reserve(m_Pending.size());
			retiringIndices.reserve(retiring.size());
			for (auto& pair : m_Pending)
			{
				pendingIndices.emplace(pair.first, (u32)keys.size());
				keys.push_back(pair.first);
			}
			const u32 pendingCount = (u32)keys.size();
			for (const ChunkKey& key : retiring)
			{
				retiringIndices.emplace(key, (u32)keys.size());
				keys.push_back(key);
			}

			std::vector<u32> parents(keys.size());
			for (u32 i = 0; i < (u32)parents.size(); ++i)
			{
				parents[i] = i;
			}
			auto Find = [&parents](u32 i)
			{
				while (parents[i] != i)
				{
					parents[i] = parents[parents[i]];
					i = parents[i];
				}
				return i;
			};
			auto Union = [&](u32 a, u32 b)
			{
				a = Find(a);
				b = Find(b);
				if (a != b)
				{
					parents[a] = b;
				}
			};

			const i32 maxLevel = (i32)m_Params.maxLodLevel;
			for (u32 i = 0; i < (u32)keys.size(); ++i)
			{
				const ChunkKey& key = keys[i];
				const bool bPending = i < pendingCount;

				// Overlapping chunks (octree cells overlap iff one contains the other): check ancestors (& self)
				// in the other set. Descendants are found when they check their own ancestors.
				ChunkKey ancestor = key;
				while (true)
				{
					const std::unordered_map<ChunkKey, u32, ChunkKeyHash>& others = bPending ? retiringIndices : pendingIndices;
					auto iter = others.find(ancestor);
					if (iter != others.end())
					{
						Union(i, iter->second);
					}
					if (ancestor.level >= maxLevel)
					{
						break;
					}
					ancestor = ancestor.Parent();
				}

				if (!bPending)
				{
					continue;
				}

				// New chunks touching (by face, edge or corner) other new chunks of the same or coarser levels.
				// Finer neighbors are found when they check their coarser neighbors.
				for (i32 dz = -1; dz <= 1; ++dz)
				{
					for (i32 dy = -1; dy <= 1; ++dy)
					{
						for (i32 dx = -1; dx <= 1; ++dx)
						{
							if (dx == 0 && dy == 0 && dz == 0)
							{
								continue;
							}
							ChunkKey neighbor = { key.level, key.index + glm::ivec3(dx, dy, dz) };
							for (i32 levelOffset = 0; levelOffset <= 2 && neighbor.level <= maxLevel; ++levelOffset)
							{
								auto iter = pendingIndices.find(neighbor);
								if (iter != pendingIndices.end())
								{
									Union(i, iter->second);
									break;
								}
								neighbor = neighbor.Parent();
							}
						}
					}
				}
			}

			std::unordered_map<u32, u32> groupIndices;
			for (u32 i = 0; i < (u32)keys.size(); ++i)
			{
				const u32 root = Find(i);
				auto iter = groupIndices.find(root);
				if (iter == groupIndices.end())
				{
					iter = groupIndices.emplace(root, (u32)m_SwapGroups.size()).first;
					m_SwapGroups.emplace_back();
				}
				SwapGroup& group = m_SwapGroups[iter->second];
				if (i < pendingCount)
				{
					group.pending.push_back(keys[i]);
					m_Pending[keys[i]].group = iter->second;
				}
				else
				{
					group.retiring.push_back(keys[i]);
				}
			}
		}

		void TerrainStreamer::ApplyReadySwapGroups(std::vector<ChunkCommand>& outCommands)
		{
			bool bAllApplied = true;
			for (SwapGroup& group : m_SwapGroups)
			{
				if (group.bApplied)
				{
					continue;
				}

				if (group.retiring.empty())
				{
					// Nothing to replace, so chunks can be shown as soon as they're ready
					bool bAllShown = true;
					for (const ChunkKey& key : group.pending)
					{
						auto iter = m_Pending.find(key);
						if (iter == m_Pending.end())
						{
							continue;
						}
						if (iter->second.bDone)
						{
							bool bGroupStarted = false;
							Show(key, std::move(iter->second), outCommands, bGroupStarted);
							m_Pending.erase(iter);
						}
						else
						{
							bAllShown = false;
						}
					}
					group.bApplied = bAllShown;
				}
				else
				{
					bool bAllDone = true;
					for (const ChunkKey& key : group.pending)
					{
						auto iter = m_Pending.find(key);
						if (iter != m_Pending.end() && !iter->second.bDone)
						{
							bAllDone = false;
							break;
						}
					}

					// Swap all at once so neighboring chunks always agree on LOD transitions
					if (bAllDone)
					{
						bool bGroupStarted = false;
						for (const ChunkKey& key : group.retiring)
						{
							Hide(key, outCommands, bGroupStarted);
						}
						for (const ChunkKey& key : group.pending)
						{
							auto iter = m_Pending.find(key);
							if (iter != m_Pending.end())
							{
								Show(key, std::move(iter->second), outCommands, bGroupStarted);
								m_Pending.erase(iter);
							}
						}
						m_RetiringCount -= glm::min(m_RetiringCount, (u32)group.retiring.size());
						group.bApplied = true;
					}
				}

				bAllApplied = bAllApplied && group.bApplied;
			}

			if (bAllApplied)
			{
				m_SwapGroups.clear();
				m_RetiringCount = 0;
				m_bBatchInFlight = false;
				VerifyBatchResult();

				const real latency = std::chrono::duration<real>(std::chrono::steady_clock::now() - m_BatchStartTime).count();
				m_BatchLatency += (glm::min(latency, 3.0f) - m_BatchLatency) * 0.3f;
			}
		}

		void TerrainStreamer::VerifyBatchResult()
		{
			// Once a batch has been applied, exactly its chunks should be displayed, with the transition masks it chose
			// (which match each other's levels). Anything else (e.g. left over from an interrupted batch) would leave
			// cracks or overlaps, so it's replaced in the next batch.
			u32 mismatchCount = 0;
			for (auto& pair : m_Displayed)
			{
				auto iter = m_BatchDesired.find(pair.first);
				if (iter == m_BatchDesired.end() || iter->second != pair.second.mask)
				{
					pair.second.bStale = true;
					++mismatchCount;
				}
			}
			for (const auto& pair : m_BatchDesired)
			{
				if (m_Displayed.find(pair.first) == m_Displayed.end())
				{
					++mismatchCount;
				}
			}

			if (mismatchCount > 0)
			{
				m_bForceRefresh = true;
				++m_BatchMismatchCount;
				if (m_BatchMismatchCount <= 10)
				{
					PrintWarn("Terrain: %u displayed chunks didn't match the selected LOD after a batch, replacing them\n", mismatchCount);
				}
			}
		}

		static void EmitCommand(ChunkCommand&& command, std::vector<ChunkCommand>& outCommands, bool& bGroupStarted)
		{
			command.bAtomicWithPrevious = bGroupStarted;
			bGroupStarted = true;
			outCommands.emplace_back(std::move(command));
		}

		void TerrainStreamer::Show(const ChunkKey& key, Pending&& pending, std::vector<ChunkCommand>& outCommands, bool& bGroupStarted)
		{
			Displayed displayed = {};
			displayed.mask = pending.mask;
			displayed.handle = m_NextHandle++;
			displayed.bEmpty = pending.mesh.Empty();
			displayed.packed = std::move(pending.packed);

			if (!displayed.bEmpty)
			{
				ChunkCommand command = {};
				command.type = ChunkCommand::Type::SHOW;
				command.handle = displayed.handle;
				command.key = key;
				command.mesh = std::move(pending.mesh);
				EmitCommand(std::move(command), outCommands, bGroupStarted);
			}

			m_Displayed[key] = std::move(displayed);
		}

		void TerrainStreamer::Hide(const ChunkKey& key, std::vector<ChunkCommand>& outCommands, bool& bGroupStarted)
		{
			auto iter = m_Displayed.find(key);
			if (iter == m_Displayed.end())
			{
				return;
			}

			if (!iter->second.bEmpty)
			{
				ChunkCommand command = {};
				command.type = ChunkCommand::Type::HIDE;
				command.handle = iter->second.handle;
				command.key = key;
				EmitCommand(std::move(command), outCommands, bGroupStarted);
			}
			m_Displayed.erase(iter);
		}

		void TerrainStreamer::ApplyEdit(const Edit& edit)
		{
			ApplyEdits(&edit, 1);
		}

		void TerrainStreamer::ApplyEdits(const Edit* edits, u32 count)
		{
			if (count == 0)
			{
				return;
			}

			// All the edits' chunks are regenerated together
			glm::vec3 editMin(1e30f);
			glm::vec3 editMax(-1e30f);
			for (u32 i = 0; i < count; ++i)
			{
				m_Edits.Add(edits[i]);
				glm::vec3 boundsMin, boundsMax;
				GetEditBounds(edits[i], boundsMin, boundsMax);
				editMin = glm::min(editMin, boundsMin);
				editMax = glm::max(editMax, boundsMax);
			}
			auto Affects = [&](const ChunkKey& key)
			{
				glm::vec3 boxMin, boxMax;
				GetChunkBounds(key, boxMin, boxMax);
				const real margin = m_Params.CellSize((u32)key.level) * 2.0f + m_Density->EditBand();
				return BoxesOverlap(boxMin - margin, boxMax + margin, editMin, editMax);
			};

			std::vector<ChunkJob> jobs;

			// Regenerate displayed chunks, swapping them all in at once when done
			for (auto& pair : m_Displayed)
			{
				if (!Affects(pair.first))
				{
					continue;
				}

				ChunkJob job = MakeJob(pair.first, pair.second.mask, -1.0f);
				job.bEdit = true;

				Pending pending = {};
				pending.mask = job.mask;
				pending.jobID = job.jobID;
				pending.bDone = false;
				m_EditPending[pair.first] = std::move(pending);

				jobs.emplace_back(std::move(job));
			}

			// Chunks waiting to be shown must also include the edit
			for (auto& pair : m_Pending)
			{
				if (!Affects(pair.first))
				{
					continue;
				}

				ChunkJob job = MakeJob(pair.first, pair.second.mask, 0.0f);
				pair.second.jobID = job.jobID;
				pair.second.bDone = false;
				pair.second.mesh.Clear();
				pair.second.packed.reset();
				jobs.emplace_back(std::move(job));
			}

			m_Pool.AddJobs(std::move(jobs));

			// Edits can create surfaces in chunks which were previously skipped as empty
			m_bForceRefresh = true;
		}

		void TerrainStreamer::SetEdits(const std::vector<Edit>& edits)
		{
			m_Edits.Clear();
			for (const Edit& edit : edits)
			{
				m_Edits.Add(edit);
			}

			if (m_Density != nullptr)
			{
				// Regenerate everything (existing chunks remain visible until replaced)
				CancelDesiredComputation();
				m_Pending.clear();
				m_EditPending.clear();
				m_SwapGroups.clear();
				m_RetiringCount = 0;
				m_Pool.SetJobs({});
				m_bBatchInFlight = false;
				RetireAllDisplayed();
				m_bForceRefresh = true;
			}
		}

		void TerrainStreamer::RetireAllDisplayed()
		{
			// A flag alone isn't enough: the batch it applies to can be superseded (e.g. when the camera teleports, as
			// it does when a world is loaded from the menu) by one which would keep old chunks matching desired keys
			for (auto& pair : m_Displayed)
			{
				pair.second.bStale = true;
			}
			m_bRetireAllOnNextBatch = true;
		}

		bool TerrainStreamer::Raycast(const glm::vec3& origin, const glm::vec3& dir, real maxDistance, glm::vec3& outHitPos, glm::vec3& outHitNormal) const
		{
			if (m_Density == nullptr || maxDistance <= 0.0f)
			{
				return false;
			}

			const glm::vec3 direction = glm::normalize(dir);
			const glm::vec3 end = origin + direction * maxDistance;
			std::vector<Edit> edits;
			m_Edits.Gather(glm::min(origin, end) - 1.0f - m_Density->EditBand(), glm::max(origin, end) + 1.0f + m_Density->EditBand(), edits);
			EditList editList;
			editList.edits = edits.data();
			editList.count = (u32)edits.size();

			const TerrainDensity& density = *m_Density;
			auto Sample = [&](real t)
			{
				return density.Sample(origin + direction * t, editList);
			};

			// The density is roughly the vertical distance to the surface in meters, so (mostly) vertical rays can take
			// steps proportional to it (scaled down to account for 3D noise). Horizontal rays can't, since a cliff may be
			// just ahead while the ground below is far away. Near the surface the minimum step is always used.
			const real step = glm::max(m_Params.voxelSize * 0.4f, 0.1f);
			const real distanceStepScale = 0.4f * glm::smoothstep(0.75f, 0.95f, glm::abs(direction.y));
			real prevT = 0.0f;
			real value = Sample(0.0f);
			if (value < 0.0f)
			{
				// Starting inside terrain
				return false;
			}

			for (real t = glm::max(step, value * distanceStepScale); prevT < maxDistance; t = prevT + glm::max(step, value * distanceStepScale))
			{
				const real tClamped = glm::min(t, maxDistance);
				value = Sample(tClamped);
				if (value < 0.0f)
				{
					real lo = prevT;
					real hi = tClamped;
					for (i32 i = 0; i < 10; ++i)
					{
						const real mid = (lo + hi) * 0.5f;
						if (Sample(mid) < 0.0f)
						{
							hi = mid;
						}
						else
						{
							lo = mid;
						}
					}

					outHitPos = origin + direction * hi;
					const real e = glm::max(m_Params.voxelSize * 0.25f, 0.05f);
					const glm::vec3 gradient(
						density.Sample(outHitPos + glm::vec3(e, 0, 0), editList) - density.Sample(outHitPos - glm::vec3(e, 0, 0), editList),
						density.Sample(outHitPos + glm::vec3(0, e, 0), editList) - density.Sample(outHitPos - glm::vec3(0, e, 0), editList),
						density.Sample(outHitPos + glm::vec3(0, 0, e), editList) - density.Sample(outHitPos - glm::vec3(0, 0, e), editList));
					const real gradientLength = glm::length(gradient);
					outHitNormal = gradientLength > 1e-8f ? gradient / gradientLength : glm::vec3(0.0f, 1.0f, 0.0f);
					return true;
				}
				prevT = tClamped;
			}

			return false;
		}

		void TerrainStreamer::FindPlants(const glm::vec3& boxMin, const glm::vec3& boxMax, std::vector<PlantInfo>& outPlants) const
		{
			if (m_Density == nullptr)
			{
				return;
			}

			std::vector<Edit> edits;
			const real margin = m_Density->EditBand() + 4.0f;
			m_Edits.Gather(boxMin - margin, boxMax + margin, edits);
			EditList editList;
			editList.edits = edits.data();
			editList.count = (u32)edits.size();
			terrain::FindPlants(*m_Density, editList, boxMin, boxMax, outPlants);
		}

		real TerrainStreamer::SampleDensity(const glm::vec3& pos) const
		{
			if (m_Density == nullptr)
			{
				return 1.0f;
			}

			std::vector<Edit> edits;
			const real band = m_Density->EditBand();
			m_Edits.Gather(pos - band, pos + band, edits);
			EditList editList;
			editList.edits = edits.data();
			editList.count = (u32)edits.size();
			return m_Density->Sample(pos, editList);
		}

		SurfaceAttributes TerrainStreamer::SampleSurface(const glm::vec3& pos, const glm::vec3& normal) const
		{
			if (m_Density == nullptr)
			{
				return {};
			}

			std::vector<Edit> edits;
			const real band = m_Density->EditBand();
			m_Edits.Gather(pos - band, pos + band, edits);
			EditList editList;
			editList.edits = edits.data();
			editList.count = (u32)edits.size();
			return m_Density->ComputeSurface(pos, normal, m_Density->SampleColumn(pos.x, pos.z), editList);
		}

		void TerrainStreamer::WaitForSnapshotWrite()
		{
			if (m_SnapshotWriteFuture.valid())
			{
				m_SnapshotWriteFuture.wait();
				m_SnapshotWriteFuture = {};
			}
		}

		void TerrainStreamer::SaveSnapshot(const glm::vec3& cameraPos)
		{
			// Don't overwrite the snapshot before it's been loaded
			if (m_SnapshotPath.empty() || m_SnapshotFuture.valid())
			{
				return;
			}

			WaitForSnapshotWrite();

			struct Entry
			{
				ChunkKey key;
				TransitionMask mask;
				std::shared_ptr<const PackedChunk> packed;
			};

			std::vector<Entry> entries;
			entries.reserve(m_Displayed.size());
			for (const auto& pair : m_Displayed)
			{
				if (pair.second.packed != nullptr)
				{
					entries.push_back(Entry{ pair.first, pair.second.mask, pair.second.packed });
				}
			}

			if (entries.empty())
			{
				return;
			}

			const std::string path = m_SnapshotPath;
			const u64 contentHash = m_Params.ContentHash();
			const u64 editsHash = m_Edits.Hash();
			m_SnapshotWriteFuture = std::async(std::launch::async, [entries = std::move(entries), path, contentHash, editsHash, cameraPos]()
			{
				const std::string tempPath = path + ".tmp";
				{
					std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
					if (!file)
					{
						return;
					}

					std::vector<u8> header;
					ByteWriter writer(header);
					writer.Write(SNAPSHOT_FILE_MAGIC);
					writer.Write(SNAPSHOT_FILE_VERSION);
					writer.Write(contentHash);
					writer.Write(editsHash);
					writer.Write(cameraPos.x);
					writer.Write(cameraPos.y);
					writer.Write(cameraPos.z);
					writer.Write((u32)entries.size());
					file.write((const char*)header.data(), (std::streamsize)header.size());

					for (const Entry& entry : entries)
					{
						header.clear();
						writer.Write(entry.key.level);
						writer.Write(entry.key.index.x);
						writer.Write(entry.key.index.y);
						writer.Write(entry.key.index.z);
						writer.Write(entry.mask);
						writer.Write((u32)entry.packed->size());
						file.write((const char*)header.data(), (std::streamsize)header.size());
						file.write((const char*)entry.packed->data(), (std::streamsize)entry.packed->size());
					}

					if (!file)
					{
						return;
					}
				}
				std::remove(path.c_str());
				std::rename(tempPath.c_str(), path.c_str());
			});
		}

		TerrainStreamer::Snapshot TerrainStreamer::LoadSnapshot(const std::string& path, u64 contentHash, u64 editsHash, std::shared_ptr<std::atomic<bool>> cancelled)
		{
			Snapshot snapshot = {};
			const std::chrono::steady_clock::time_point loadStart = std::chrono::steady_clock::now();

			{
				// Check the header before reading the whole (large) file
				std::ifstream file(path, std::ios::binary);
				u8 headerBytes[sizeof(u32) * 2 + sizeof(u64) * 2] = {};
				if (!file || !file.read((char*)headerBytes, sizeof(headerBytes)))
				{
					return snapshot;
				}
				ByteReader headerReader(headerBytes, sizeof(headerBytes));
				u32 magic, version;
				u64 fileContentHash, fileEditsHash;
				if (!headerReader.Read(magic) || !headerReader.Read(version) || !headerReader.Read(fileContentHash) || !headerReader.Read(fileEditsHash) ||
					magic != SNAPSHOT_FILE_MAGIC || version != SNAPSHOT_FILE_VERSION || fileContentHash != contentHash || fileEditsHash != editsHash)
				{
					return snapshot;
				}
			}

			std::vector<u8> bytes;
			if (cancelled->load() || !ReadWholeFile(path, bytes) || cancelled->load())
			{
				return snapshot;
			}
			const ms readTime = std::chrono::duration<ms, std::milli>(std::chrono::steady_clock::now() - loadStart).count();

			ByteReader reader(bytes.data(), bytes.size());
			u32 magic, version, chunkCount;
			u64 fileContentHash, fileEditsHash;
			if (!reader.Read(magic) || !reader.Read(version) || !reader.Read(fileContentHash) || !reader.Read(fileEditsHash) ||
				!reader.Read(snapshot.cameraPos.x) || !reader.Read(snapshot.cameraPos.y) || !reader.Read(snapshot.cameraPos.z) ||
				!reader.Read(chunkCount))
			{
				return snapshot;
			}

			if (magic != SNAPSHOT_FILE_MAGIC || version != SNAPSHOT_FILE_VERSION || fileContentHash != contentHash || fileEditsHash != editsHash)
			{
				return snapshot;
			}

			std::vector<std::pair<const u8*, u32>> chunkData;
			chunkData.reserve(chunkCount);
			snapshot.keys.reserve(chunkCount);
			for (u32 i = 0; i < chunkCount; ++i)
			{
				ChunkKey key;
				TransitionMask mask;
				u32 size;
				if (!reader.Read(key.level) || !reader.Read(key.index.x) || !reader.Read(key.index.y) || !reader.Read(key.index.z) ||
					!reader.Read(mask) || !reader.Read(size))
				{
					return Snapshot{};
				}

				const u8* data = reader.Current();
				if (!reader.Skip(size))
				{
					return Snapshot{};
				}

				snapshot.keys.emplace_back(key, mask);
				chunkData.emplace_back(data, size);
			}

			// Unpacking is most of the work, so it's spread over the job system's workers
			snapshot.meshes.resize(chunkCount);
			snapshot.packed.resize(chunkCount);
			std::atomic<bool> bFailed{ false };
			auto Unpack = [&](u32 i)
			{
				const u8* data = chunkData[i].first;
				const u32 size = chunkData[i].second;
				if (bFailed.load(std::memory_order_relaxed) || cancelled->load(std::memory_order_relaxed))
				{
					return;
				}
				if (!UnpackChunkMesh(data, size, snapshot.meshes[i]))
				{
					bFailed = true;
					return;
				}
				snapshot.packed[i] = std::make_shared<const PackedChunk>(data, data + size);
			};
			if (JobSystem::GetThreadCount() > 0)
			{
				JobSystem::Context context;
				JobSystem::Dispatch(context, chunkCount, 32, [&](JobSystem::JobArgs args) { Unpack(args.jobIndex); });
				JobSystem::Wait(context);
			}
			else
			{
				// (Shutting down)
				for (u32 i = 0; i < chunkCount; ++i)
				{
					Unpack(i);
				}
			}
			if (bFailed || cancelled->load())
			{
				return Snapshot{};
			}

			snapshot.bValid = true;
			snapshot.readTime = readTime;
			snapshot.loadTime = std::chrono::duration<ms, std::milli>(std::chrono::steady_clock::now() - loadStart).count();
			return snapshot;
		}

		void TerrainStreamer::ApplySnapshot(Snapshot&& snapshot, std::vector<ChunkCommand>& outCommands)
		{
			const bool bReplaceDisplayed = m_bSnapshotReplacesDisplayed;
			m_bSnapshotReplacesDisplayed = false;
			if (!snapshot.bValid)
			{
				return;
			}
			if (!m_Displayed.empty())
			{
				if (!bReplaceDisplayed)
				{
					return;
				}
				// The previous world's chunks (which would otherwise remain until all of their replacements were
				// generated)
				HideAll(outCommands);
			}
			Print("Terrain snapshot: %u chunks loaded in %.0f ms (%.0f ms reading)\n", (u32)snapshot.keys.size(), snapshot.loadTime, snapshot.readTime);

			for (size_t i = 0; i < snapshot.keys.size(); ++i)
			{
				Pending pending = {};
				pending.mask = snapshot.keys[i].second;
				pending.bDone = true;
				pending.mesh = std::move(snapshot.meshes[i]);
				pending.packed = std::move(snapshot.packed[i]);
				// Each chunk can be shown independently since they were all displayed together
				bool bGroupStarted = false;
				Show(snapshot.keys[i].first, std::move(pending), outCommands, bGroupStarted);
			}

			m_SnapshotChunkCount = (u32)snapshot.keys.size();
			m_bRetireAllOnNextBatch = false;
			m_bForceRefresh = true;
		}

		void TerrainStreamer::Update(const glm::vec3& cameraPos, std::vector<ChunkCommand>& outCommands)
		{
			PROFILE_AUTO("TerrainStreamer Update");

			UpdateVelocity(cameraPos);

			for (auto iter = m_DiscardedSnapshotFutures.begin(); iter != m_DiscardedSnapshotFutures.end();)
			{
				iter = iter->wait_for(std::chrono::seconds(0)) == std::future_status::ready ? m_DiscardedSnapshotFutures.erase(iter) : iter + 1;
			}

			if (m_SnapshotFuture.valid())
			{
				// Loading the snapshot is quick, wait for it rather than generating chunks which may then be discarded
				if (m_SnapshotFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
				{
					return;
				}
				ApplySnapshot(m_SnapshotFuture.get(), outCommands);
			}

			{
				PROFILE_AUTO("TerrainStreamer take results");
				static std::vector<ChunkResult> results;
				results.clear();
				m_Pool.TakeResults(results);
				for (ChunkResult& result : results)
				{
					Pending* pending = nullptr;
					auto editIter = m_EditPending.find(result.key);
					if (editIter != m_EditPending.end() && editIter->second.jobID == result.jobID)
					{
						pending = &editIter->second;
					}
					else
					{
						auto iter = m_Pending.find(result.key);
						if (iter != m_Pending.end() && iter->second.jobID == result.jobID)
						{
							pending = &iter->second;
						}
					}

					if (pending != nullptr)
					{
						pending->mesh = std::move(result.mesh);
						pending->packed = std::move(result.packed);
						pending->bDone = true;

						++m_TotalGenerated;
						m_TotalGenerationTime += result.generationTime;
					}
				}
			}

			// Swap in chunks regenerated after edits all at once, so neighbors always agree on edited density
			if (!m_EditPending.empty())
			{
				bool bAllDone = true;
				for (auto& pair : m_EditPending)
				{
					bAllDone = bAllDone && pair.second.bDone;
				}

				if (bAllDone)
				{
					bool bGroupStarted = false;
					for (auto& pair : m_EditPending)
					{
						auto displayedIter = m_Displayed.find(pair.first);
						if (displayedIter == m_Displayed.end() || displayedIter->second.mask != pair.second.mask)
						{
							// Replaced by an LOD change in the meantime
							continue;
						}

						Hide(pair.first, outCommands, bGroupStarted);
						Show(pair.first, std::move(pair.second), outCommands, bGroupStarted);
					}
					m_EditPending.clear();
				}
			}

			// LOD is selected around where the camera is expected to be by the time the batch is ready (so fast
			// moving cameras don't outrun the detailed region)
			const real predictionTime = glm::clamp(m_BatchLatency + 0.15f, 0.0f, 2.0f);
			glm::vec3 predictedOffset = m_SmoothedVelocity * predictionTime;
			const real maxPredictionDistance = m_Params.ChunkSize(4);
			if (glm::length(predictedOffset) > maxPredictionDistance)
			{
				predictedOffset *= maxPredictionDistance / glm::length(predictedOffset);
			}
			const glm::vec3 selectionPos = cameraPos + predictedOffset;

			// LOD selection runs in the background. Batches must complete before the next starts (since all
			// changes generally connect into one group), except after teleports, where in-flight work is reused.
			if (m_DesiredFuture.valid())
			{
				if (m_DesiredFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
				{
					PROFILE_AUTO("TerrainStreamer StartBatch");
					StartBatch(m_DesiredFuture.get());
				}
			}
			else
			{
				const real distanceFromBatch = glm::distance(selectionPos, m_BatchCameraPos);
				const bool bRefresh = m_bBatchInFlight ?
					distanceFromBatch > m_Params.ChunkSize(3) * 3.0f :
					(m_bForceRefresh || distanceFromBatch > m_Params.ChunkSize(0) * 0.2f);
				if (bRefresh)
				{
					m_bForceRefresh = false;
					m_BatchStartTime = std::chrono::steady_clock::now();

					// Only accessed by the LOD selection task while it's in flight
					// Merge edits into coarse cells to keep the per node test cheap
					std::unordered_map<glm::ivec3, std::pair<glm::vec3, glm::vec3>, EditBoundsCellHash> editCells;
					for (const Edit& edit : m_Edits.Edits())
					{
						const glm::ivec3 cell = glm::ivec3(glm::floor(edit.center / 64.0f));
						glm::vec3 boundsMin, boundsMax;
						GetEditBounds(edit, boundsMin, boundsMax);
						auto iter = editCells.find(cell);
						if (iter == editCells.end())
						{
							editCells.emplace(cell, std::make_pair(boundsMin, boundsMax));
						}
						else
						{
							iter->second.first = glm::min(iter->second.first, boundsMin);
							iter->second.second = glm::max(iter->second.second, boundsMax);
						}
					}
					m_EditBoundsForSelection.clear();
					for (const auto& pair : editCells)
					{
						m_EditBoundsForSelection.push_back(pair.second.first);
						m_EditBoundsForSelection.push_back(pair.second.second);
					}

					m_DesiredFuture = std::async(std::launch::async, [this, selectionPos]()
					{
						DesiredChunks desired;
						desired.cameraPos = selectionPos;
						ComputeDesiredChunks(selectionPos, desired.chunks);
						return desired;
					});
				}
			}

			if (m_bBatchInFlight)
			{
				PROFILE_AUTO("TerrainStreamer ApplyReadySwapGroups");
				ApplyReadySwapGroups(outCommands);
			}
		}

		void TerrainStreamer::HideAll(std::vector<ChunkCommand>& outCommands)
		{
			CancelDesiredComputation();

			for (auto& pair : m_Displayed)
			{
				if (!pair.second.bEmpty)
				{
					ChunkCommand command = {};
					command.type = ChunkCommand::Type::HIDE;
					command.handle = pair.second.handle;
					command.key = pair.first;
					outCommands.emplace_back(std::move(command));
				}
			}
			m_Displayed.clear();
			m_SwapGroups.clear();
			m_RetiringCount = 0;
			m_Pending.clear();
			m_EditPending.clear();
			m_Pool.SetJobs({});
			m_bBatchInFlight = false;
			m_bForceRefresh = true;
		}

		TerrainStreamer::Stats TerrainStreamer::GetStats()
		{
			Stats stats = {};
			stats.displayedChunkCount = (u32)m_Displayed.size();
			for (auto& pair : m_Displayed)
			{
				if (!pair.second.bEmpty)
				{
					++stats.displayedNonEmptyChunkCount;
					if (pair.first.level >= 0 && pair.first.level < 32)
					{
						++stats.chunksPerLevel[pair.first.level];
					}
				}
				if (pair.second.packed != nullptr)
				{
					stats.packedBytes += pair.second.packed->size();
				}
			}
			stats.pendingChunkCount = (u32)m_Pending.size();
			stats.queuedJobCount = m_Pool.QueuedJobCount();
			stats.retiringChunkCount = m_RetiringCount;
			stats.swapGroupCount = (u32)m_SwapGroups.size();
			for (const SwapGroup& group : m_SwapGroups)
			{
				stats.pendingSwapGroupCount += group.bApplied ? 0 : 1;
			}
			stats.velocity = m_SmoothedVelocity;
			stats.batchLatency = m_BatchLatency;
			stats.bSettled = !m_bBatchInFlight && !m_DesiredFuture.valid() && !m_bForceRefresh && m_EditPending.empty() && !m_SnapshotFuture.valid();
			stats.desiredChunkCount = m_DesiredCount;
			stats.editJobCount = (u32)m_EditPending.size();
			stats.totalGenerated = m_TotalGenerated;
			stats.averageGenerationTime = m_TotalGenerated > 0 ? m_TotalGenerationTime / (real)m_TotalGenerated : 0.0f;
			stats.threadCount = m_Pool.ThreadCount();
			stats.snapshotChunkCount = m_SnapshotChunkCount;
			stats.bSnapshotLoading = m_SnapshotFuture.valid();
			return stats;
		}

		u32 TerrainStreamer::ValidateDisplayedMasks(std::string* outReport /* = nullptr */) const
		{
			// (-1: none)
			auto FindDisplayedLevel = [this](const glm::vec3& pos, bool& bOutNonEmpty) -> i32
			{
				for (i32 level = 0; level <= (i32)m_Params.maxLodLevel; ++level)
				{
					const ChunkKey key = { level, glm::ivec3(glm::floor(pos / m_Params.ChunkSize((u32)level))) };
					auto iter = m_Displayed.find(key);
					if (iter != m_Displayed.end())
					{
						bOutNonEmpty = bOutNonEmpty || !iter->second.bEmpty;
						return level;
					}
				}
				return -1;
			};

			u32 errorCount = 0;
			for (const auto& pair : m_Displayed)
			{
				const ChunkKey& key = pair.first;
				const real size = m_Params.ChunkSize((u32)key.level);
				const glm::vec3 center = (glm::vec3(key.index) + 0.5f) * size;
				// Faces (axis, side) & edges (axis, side0, side1), probed at the centers of the finer cells across them
				for (u32 element = 0; element < 18; ++element)
				{
					const bool bFace = element < 6;
					const u32 axis = bFace ? element / 2 : (element - 6) / 4;
					const u32 o0 = axis == 0 ? 1 : 0;
					const u32 o1 = axis == 2 ? 1 : 2;
					glm::vec3 offset(0.0f);
					u32 bit;
					if (bFace)
					{
						offset[axis] = (element % 2) ? 1.0f : -1.0f;
						bit = FaceBit(axis, element % 2);
					}
					else
					{
						const u32 s0 = (element - 6) & 1;
						const u32 s1 = ((element - 6) >> 1) & 1;
						offset[o0] = s0 ? 1.0f : -1.0f;
						offset[o1] = s1 ? 1.0f : -1.0f;
						bit = EdgeBit(axis, s0, s1);
					}

					bool bFiner = false;
					bool bTooFine = false;
					bool bNeighborNonEmpty = false;
					for (i32 t0 = -1; t0 <= 1; t0 += 2)
					{
						for (i32 t1 = -1; t1 <= 1; t1 += 2)
						{
							glm::vec3 probe = center + offset * (size * 0.75f);
							// Along the face's (or edge's) tangent directions
							if (bFace)
							{
								probe[axis == 0 ? 1 : 0] += (real)t0 * size * 0.25f;
								probe[axis == 2 ? 1 : 2] += (real)t1 * size * 0.25f;
							}
							else
							{
								probe[axis] += (real)t0 * size * 0.25f;
							}
							const i32 level = FindDisplayedLevel(probe, bNeighborNonEmpty);
							bFiner = bFiner || (level >= 0 && level < key.level);
							bTooFine = bTooFine || (level >= 0 && level < key.level - 1);
						}
					}

					const bool bMaskFiner = (pair.second.mask & bit) != 0;
					// Face bits imply the bits of their edges, so an edge bit without a finer neighbor is fine there
					const bool bImplied = !bFace && (pair.second.mask & (FaceBit(o0, (element - 6) & 1) | FaceBit(o1, ((element - 6) >> 1) & 1))) != 0;
					if (bTooFine || (bFiner && !bMaskFiner) || (!bFiner && bMaskFiner && !bImplied && !pair.second.bEmpty && bNeighborNonEmpty))
					{
						++errorCount;
						if (outReport != nullptr && errorCount <= 10)
						{
							char line[384];
							snprintf(line, sizeof(line), "chunk L%d (%d, %d, %d) mask %x (now desired: %x, leaf level %d, stale %d): %s %u%s neighbor %s\n", key.level, key.index.x, key.index.y, key.index.z,
								pair.second.mask, ComputeTransitionMask(m_BatchCameraPos, key), LeafLevelAt(m_BatchCameraPos, center), (i32)pair.second.bStale,
								bFace ? "face" : "edge", element, bTooFine ? " (2+ levels finer)" : "", bFiner ? "is finer but not in the mask" : "isn't finer");
							*outReport += line;
						}
					}
				}
			}
			return errorCount;
		}

		void TerrainStreamer::GetDisplayedChunks(std::vector<DisplayedChunkInfo>& outChunks) const
		{
			outChunks.clear();
			outChunks.reserve(m_Displayed.size() + m_Pending.size());
			for (const auto& pair : m_Displayed)
			{
				outChunks.push_back(DisplayedChunkInfo{ pair.first, pair.second.mask, pair.second.bEmpty, false });
			}
			for (const auto& pair : m_Pending)
			{
				outChunks.push_back(DisplayedChunkInfo{ pair.first, pair.second.mask, false, true });
			}
		}
	} // namespace terrain
} // namespace flex
