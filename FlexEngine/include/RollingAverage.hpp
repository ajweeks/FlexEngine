#pragma once

#include "Types.hpp"

namespace flex
{
	template <class T>
	struct RollingAverage
	{
		RollingAverage() :
			currentAverage(T()),
			samplingType(SamplingType::LINEAR)
		{
		}

		RollingAverage(i32 valueCount, SamplingType samplingType = SamplingType::CONSTANT) :
			samplingType(samplingType),
			currentAverage(T())
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
				real sampleWeight;

				switch (samplingType)
				{
				case SamplingType::CONSTANT:
				{
					sampleWeight = 1.0f;
				} break;
				case SamplingType::LINEAR:
				{
					sampleWeight = (real)((i - currentIndex + valueCount) % valueCount);
				} break;
				case SamplingType::CUBIC:
				{
					real alpha = (real)((i - currentIndex + valueCount) % valueCount) / ((real)valueCount - 1);
					sampleWeight = alpha * alpha * alpha;
				} break;
				default:
				{
					ENSURE_NO_ENTRY();
					sampleWeight = 0.0f;
				} break;
				}

				currentAverage += prevValues[i] * sampleWeight;
				sampleCount += (i32)sampleWeight;
			}

			// TODO: Somehow make this frame-rate independent even when added to every frame!
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
