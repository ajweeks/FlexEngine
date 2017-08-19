#pragma once

#include <random>

namespace flex
{
	class Random final
	{
	public:
		void InitializeSeedRandom();
		void InitializeSeed(unsigned int seed);

		int IntRange(int min, int max);
		float FloatRange(float min, float max);
		double DoubleRange(double min, double max);

	private:
		Random() = delete;
		~Random() = delete;

		Random(const Random&) = delete;
		Random& operator=(const Random&) = delete;

	};
} // namespace flex
