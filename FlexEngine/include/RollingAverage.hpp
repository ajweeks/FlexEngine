#pragma once

#include "Types.hpp"

namespace flex
{
	template <class T>
	struct RollingAverage
	{
		RollingAverage()
		{
		}

		RollingAverage(i32 valueCount, SamplingType samplingType = SamplingType::CONSTANT) :
			samplingType(samplingType)
		{
			prevValues.resize(valueCount);
		}

		void AddValue(T newValue)
		{
			prevValues[currentIndex++] = newValue;
			currentIndex %= prevValues.size();

			currentAverage = T();
			i32 sampleCount = 0;
			i32 valueCount = (i32)prevValues.size();
			for (i32 i = 0; i < valueCount; ++i)
			{
				switch (samplingType)
				{
				case SamplingType::CONSTANT:
					currentAverage += prevValues[i];
					sampleCount++;
					break;
				case SamplingType::LINEAR:
					real sampleWeight = (real)(i <= currentIndex ? i + (valueCount - currentIndex) : i - currentIndex);
					currentAverage += prevValues[i] * sampleWeight;
					sampleCount += (i32)sampleWeight;
					break;
				}
			}

			if (sampleCount > 0)
			{
				currentAverage /= sampleCount;
			}
		}

		void Reset()
		{
			std::fill(prevValues.begin(), prevValues.end(), T());
			currentIndex = 0;
			currentAverage = T();
		}

		void Reset(const T& resetValue)
		{
			std::fill(prevValues.begin(), prevValues.end(), resetValue);
			currentIndex = 0;
			currentAverage = resetValue;
		}

		T currentAverage;
		std::vector<T> prevValues;

		SamplingType samplingType;

		i32 currentIndex = 0;
	};

} // namespace flex
