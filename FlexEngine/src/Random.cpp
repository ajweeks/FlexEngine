#include "stdafx.h"

#include "Random.h"

#include <time.h>

#include <glm/gtc/random.hpp>

namespace flex
{
	void Random::InitializeSeedRandom()
	{
		srand((unsigned int)time(0));
	}

	void Random::InitializeSeed(unsigned int seed)
	{
		srand(seed);
	}

	int Random::IntRange(int min, int max)
	{
		return glm::linearRand(min, max);
	}

	float Random::FloatRange(float min, float max)
	{
		return glm::linearRand(min, max);
	}

	double Random::DoubleRange(double min, double max)
	{
		return glm::linearRand(min, max);
	}
} // namespace flex
