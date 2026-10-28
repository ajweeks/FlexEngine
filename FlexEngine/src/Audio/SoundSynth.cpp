#include "stdafx.hpp"

#include "Audio/SoundSynth.hpp"

namespace flex
{
	namespace synth
	{
		const char* BuildStamp()
		{
			return __DATE__ " " __TIME__;
		}

		u32 HashSeed(const char* name, u32 variant)
		{
			// FNV-1a, then mixed with the variant
			u32 hash = 2166136261u;
			for (const char* c = name; *c != '\0'; ++c)
			{
				hash = (hash ^ (u32)(u8)*c) * 16777619u;
			}
			hash ^= (variant + 1u) * 0x9E3779B9u;
			hash ^= hash >> 16;
			hash *= 0x85EBCA6Bu;
			hash ^= hash >> 13;
			hash *= 0xC2B2AE35u;
			hash ^= hash >> 16;
			return hash == 0 ? 0x1234567u : hash;
		}

		Rng::Rng(u32 seed) :
			state(seed == 0 ? 0x9E3779B9u : seed)
		{
		}

		u32 Rng::NextU32()
		{
			// xorshift32
			u32 x = state;
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			state = x;
			return x;
		}

		real Rng::Next01()
		{
			return (real)(NextU32() >> 8) * (1.0f / 16777216.0f);
		}

		real Rng::NextSigned()
		{
			return Next01() * 2.0f - 1.0f;
		}

		real Rng::Range(real min, real max)
		{
			return min + (max - min) * Next01();
		}

		i32 Rng::RangeInt(i32 min, i32 maxInclusive)
		{
			return min + (i32)(NextU32() % (u32)(maxInclusive - min + 1));
		}

		real Rng::NextPoissonInterval(real ratePerSec)
		{
			return -std::log(1.0f - Next01() * 0.9999f) / ratePerSec;
		}

		Buffer::Buffer(sec length, u32 sampleRate /* = SAMPLE_RATE */) :
			samples((size_t)std::max((i32)(length * (real)sampleRate), 1), 0.0f),
			sampleRate(sampleRate)
		{
		}

		u32 Buffer::IndexOf(sec time) const
		{
			return (u32)std::max((i32)(time * (real)sampleRate), 0);
		}

		real PinkNoise::Next()
		{
			const real white = rng.NextSigned();
			b0 = 0.99765f * b0 + white * 0.0990460f;
			b1 = 0.96300f * b1 + white * 0.2965164f;
			b2 = 0.57000f * b2 + white * 1.0526913f;
			return (b0 + b1 + b2 + white * 0.1848f) * 0.25f;
		}

		real BrownNoise::Next()
		{
			value = value * 0.996f + rng.NextSigned() * 0.06f;
			return std::max(std::min(value * 2.5f, 1.0f), -1.0f);
		}

		real Oscillator::Advance(real freq)
		{
			const real p = phase;
			phase += freq / (real)sampleRate;
			phase -= std::floor(phase);
			return p;
		}

		real Oscillator::Sine(real freq)
		{
			return std::sin(Advance(freq) * SYNTH_TWO_PI);
		}

		real Oscillator::Saw(real freq)
		{
			return Advance(freq) * 2.0f - 1.0f;
		}

		real Oscillator::Square(real freq, real pulseWidth /* = 0.5f */)
		{
			return Advance(freq) < pulseWidth ? 1.0f : -1.0f;
		}

		real Oscillator::Triangle(real freq)
		{
			const real p = Advance(freq);
			return 1.0f - 4.0f * std::abs(p - 0.5f);
		}

		Biquad::Biquad(Type type, real freq, real q, real gainDb /* = 0.0f */, u32 sampleRate /* = SAMPLE_RATE */)
		{
			Set(type, freq, q, gainDb, sampleRate);
		}

		void Biquad::Set(Type type, real freq, real q, real gainDb /* = 0.0f */, u32 sampleRate /* = SAMPLE_RATE */)
		{
			freq = std::max(std::min(freq, (real)sampleRate * 0.49f), 10.0f);
			q = std::max(q, 0.05f);
			const real w0 = SYNTH_TWO_PI * freq / (real)sampleRate;
			const real cosW0 = std::cos(w0);
			const real alpha = std::sin(w0) / (2.0f * q);
			real nb0, nb1, nb2, na0, na1, na2;
			switch (type)
			{
			case Type::LOWPASS:
				nb0 = (1.0f - cosW0) * 0.5f; nb1 = 1.0f - cosW0; nb2 = nb0;
				na0 = 1.0f + alpha; na1 = -2.0f * cosW0; na2 = 1.0f - alpha;
				break;
			case Type::HIGHPASS:
				nb0 = (1.0f + cosW0) * 0.5f; nb1 = -(1.0f + cosW0); nb2 = nb0;
				na0 = 1.0f + alpha; na1 = -2.0f * cosW0; na2 = 1.0f - alpha;
				break;
			case Type::BANDPASS:
				nb0 = alpha; nb1 = 0.0f; nb2 = -alpha;
				na0 = 1.0f + alpha; na1 = -2.0f * cosW0; na2 = 1.0f - alpha;
				break;
			case Type::PEAK:
			{
				const real a = std::pow(10.0f, gainDb / 40.0f);
				nb0 = 1.0f + alpha * a; nb1 = -2.0f * cosW0; nb2 = 1.0f - alpha * a;
				na0 = 1.0f + alpha / a; na1 = -2.0f * cosW0; na2 = 1.0f - alpha / a;
			} break;
			case Type::NOTCH:
			default:
				nb0 = 1.0f; nb1 = -2.0f * cosW0; nb2 = 1.0f;
				na0 = 1.0f + alpha; na1 = -2.0f * cosW0; na2 = 1.0f - alpha;
				break;
			}
			b0 = nb0 / na0;
			b1 = nb1 / na0;
			b2 = nb2 / na0;
			a1 = na1 / na0;
			a2 = na2 / na0;
		}

		real Biquad::Process(real x)
		{
			const real y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
			x2 = x1;
			x1 = x;
			y2 = y1;
			// Flush denormals
			y1 = std::abs(y) < 1e-20f ? 0.0f : y;
			return y;
		}

		void Biquad::Reset()
		{
			x1 = x2 = y1 = y2 = 0.0f;
		}

		void OnePoleLowpass::SetCutoff(real freq, u32 sampleRate /* = SAMPLE_RATE */)
		{
			coeff = 1.0f - std::exp(-SYNTH_TWO_PI * freq / (real)sampleRate);
		}

		real EnvAttackDecay(real t, real attack, real decay)
		{
			if (t < 0.0f)
			{
				return 0.0f;
			}
			if (t < attack)
			{
				return t / attack;
			}
			return std::exp(-(t - attack) / std::max(decay, 1e-5f));
		}

		real EnvAttackHoldRelease(real t, real attack, real hold, real release)
		{
			if (t < 0.0f)
			{
				return 0.0f;
			}
			if (t < attack)
			{
				return t / attack;
			}
			t -= attack;
			if (t < hold)
			{
				return 1.0f;
			}
			t -= hold;
			return std::max(1.0f - t / std::max(release, 1e-5f), 0.0f);
		}

		real EnvBump(real t, real peak, real length)
		{
			if (t <= 0.0f || t >= length)
			{
				return 0.0f;
			}
			if (t < peak)
			{
				const real x = t / peak;
				return x * x * (3.0f - 2.0f * x);
			}
			const real x = 1.0f - (t - peak) / (length - peak);
			return x * x * (3.0f - 2.0f * x);
		}

		real ExpSweep(real t, real duration, real f0, real f1)
		{
			const real alpha = Clamp01(t / std::max(duration, 1e-5f));
			return f0 * std::pow(f1 / f0, alpha);
		}

		real Lerp(real a, real b, real t)
		{
			return a + (b - a) * t;
		}

		real Clamp01(real x)
		{
			return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
		}

		real SmoothStep(real edge0, real edge1, real x)
		{
			const real t = Clamp01((x - edge0) / (edge1 - edge0));
			return t * t * (3.0f - 2.0f * t);
		}

		real DbToGain(real db)
		{
			return std::pow(10.0f, db / 20.0f);
		}

		void Mix(Buffer& dst, const Buffer& src, real gain /* = 1.0f */, sec offset /* = 0.0f */)
		{
			const u32 start = dst.IndexOf(offset);
			for (u32 i = 0; i < src.Count() && start + i < dst.Count(); ++i)
			{
				dst.samples[start + i] += src.samples[i] * gain;
			}
		}

		void MixWrapped(Buffer& dst, const Buffer& src, real gain, sec offset)
		{
			const u32 count = dst.Count();
			u32 index = dst.IndexOf(offset) % count;
			for (u32 i = 0; i < src.Count(); ++i)
			{
				dst.samples[index] += src.samples[i] * gain;
				if (++index == count)
				{
					index = 0;
				}
			}
		}

		void ApplyGain(Buffer& buffer, real gain)
		{
			for (real& s : buffer.samples)
			{
				s *= gain;
			}
		}

		void Normalize(Buffer& buffer, real targetPeak)
		{
			real peak = 0.0f;
			for (real s : buffer.samples)
			{
				peak = std::max(peak, std::abs(s));
			}
			if (peak > 1e-6f)
			{
				ApplyGain(buffer, targetPeak / peak);
			}
		}

		void FadeIn(Buffer& buffer, sec duration)
		{
			const u32 n = std::min(buffer.IndexOf(duration), buffer.Count());
			for (u32 i = 0; i < n; ++i)
			{
				buffer.samples[i] *= (real)i / (real)n;
			}
		}

		void FadeOut(Buffer& buffer, sec duration)
		{
			const u32 n = std::min(buffer.IndexOf(duration), buffer.Count());
			const u32 count = buffer.Count();
			for (u32 i = 0; i < n; ++i)
			{
				buffer.samples[count - 1 - i] *= (real)i / (real)n;
			}
		}

		void SoftClip(Buffer& buffer, real drive)
		{
			const real norm = 1.0f / std::tanh(drive);
			for (real& s : buffer.samples)
			{
				s = std::tanh(s * drive) * norm;
			}
		}

		void RemoveDC(Buffer& buffer)
		{
			// One pole highpass at ~10 Hz
			const real r = 1.0f - SYNTH_TWO_PI * 10.0f / (real)buffer.sampleRate;
			real px = 0.0f;
			real py = 0.0f;
			for (real& s : buffer.samples)
			{
				const real y = s - px + r * py;
				px = s;
				py = y;
				s = y;
			}
		}

		void MakeSeamlessLoop(Buffer& buffer, sec crossfade)
		{
			const u32 n = buffer.IndexOf(crossfade);
			if (n == 0 || n >= buffer.Count())
			{
				return;
			}
			const u32 loopLength = buffer.Count() - n;
			for (u32 i = 0; i < n; ++i)
			{
				// Equal power: the tail (continuing on from the loop's end) fades out as the start fades in
				const real t = ((real)i + 0.5f) / (real)n;
				const real fadeIn = std::sin(t * SYNTH_PI * 0.5f);
				const real fadeOut = std::cos(t * SYNTH_PI * 0.5f);
				buffer.samples[i] = buffer.samples[i] * fadeIn + buffer.samples[loopLength + i] * fadeOut;
			}
			buffer.samples.resize(loopLength);
		}

		Stats ComputeStats(const Buffer& buffer)
		{
			Stats stats;
			double sumSq = 0.0;
			for (real s : buffer.samples)
			{
				stats.peak = std::max(stats.peak, std::abs(s));
				sumSq += (double)s * (double)s;
			}
			stats.rms = buffer.Count() > 0 ? (real)std::sqrt(sumSq / (double)buffer.Count()) : 0.0f;
			stats.peakDb = 20.0f * std::log10(std::max(stats.peak, 1e-6f));
			stats.rmsDb = 20.0f * std::log10(std::max(stats.rms, 1e-6f));
			return stats;
		}

		void ToPCM16(const Buffer& buffer, std::vector<i16>& outSamples)
		{
			outSamples.resize(buffer.Count());
			for (u32 i = 0; i < buffer.Count(); ++i)
			{
				const real s = std::max(std::min(buffer.samples[i], 1.0f), -1.0f);
				outSamples[i] = (i16)std::lround(s * 32767.0f);
			}
		}

		bool WriteWav(const char* filePath, const Buffer& buffer)
		{
			FILE* file = fopen(filePath, "wb");
			if (file == nullptr)
			{
				return false;
			}

			std::vector<i16> pcm;
			ToPCM16(buffer, pcm);
			const u32 dataSize = (u32)(pcm.size() * sizeof(i16));
			const u32 riffSize = 36 + dataSize;
			const u32 fmtSize = 16;
			const u16 audioFormat = 1;
			const u16 channels = 1;
			const u32 sampleRate = buffer.sampleRate;
			const u32 byteRate = sampleRate * sizeof(i16);
			const u16 blockAlign = sizeof(i16);
			const u16 bitsPerSample = 16;

			fwrite("RIFF", 1, 4, file);
			fwrite(&riffSize, 4, 1, file);
			fwrite("WAVEfmt ", 1, 8, file);
			fwrite(&fmtSize, 4, 1, file);
			fwrite(&audioFormat, 2, 1, file);
			fwrite(&channels, 2, 1, file);
			fwrite(&sampleRate, 4, 1, file);
			fwrite(&byteRate, 4, 1, file);
			fwrite(&blockAlign, 2, 1, file);
			fwrite(&bitsPerSample, 2, 1, file);
			fwrite("data", 1, 4, file);
			fwrite(&dataSize, 4, 1, file);
			fwrite(pcm.data(), sizeof(i16), pcm.size(), file);
			fclose(file);
			return true;
		}
	} // namespace synth
} // namespace flex
