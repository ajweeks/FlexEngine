#pragma once

// Small procedural audio toolkit: noise colours, oscillators, filters, envelopes & buffer operations. Everything is
// deterministic (seeded) & engine independent (no OpenAL/glm), so sounds can be generated on worker threads & tested
// in a standalone harness.
namespace flex
{
	namespace synth
	{
		constexpr u32 SAMPLE_RATE = 44100;
		constexpr real SYNTH_PI = 3.14159265358979f;
		constexpr real SYNTH_TWO_PI = 6.28318530717959f;

		// Hash a name & variant index into a seed, so each variant of each sound is different but reproducible
		u32 HashSeed(const char* name, u32 variant);

		struct Rng
		{
			explicit Rng(u32 seed);

			u32 NextU32();
			// [0, 1)
			real Next01();
			// [-1, 1)
			real NextSigned();
			real Range(real min, real max);
			i32 RangeInt(i32 min, i32 maxInclusive);
			// Exponentially distributed wait for a Poisson process with the given rate (events per second)
			real NextPoissonInterval(real ratePerSec);

			u32 state;
		};

		struct Buffer
		{
			Buffer() = default;
			explicit Buffer(sec length, u32 sampleRate = SAMPLE_RATE);

			u32 Count() const { return (u32)samples.size(); }
			sec Length() const { return (real)samples.size() / (real)sampleRate; }
			u32 IndexOf(sec time) const;

			std::vector<real> samples;
			u32 sampleRate = SAMPLE_RATE;
		};

		// Noise generators. All output roughly [-1, 1].
		struct WhiteNoise
		{
			explicit WhiteNoise(u32 seed) : rng(seed) {}
			real Next() { return rng.NextSigned(); }
			Rng rng;
		};

		// -3 dB/octave (Paul Kellet's economy filter)
		struct PinkNoise
		{
			explicit PinkNoise(u32 seed) : rng(seed) {}
			real Next();
			Rng rng;
			real b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
		};

		// -6 dB/octave (leaky integrated white noise)
		struct BrownNoise
		{
			explicit BrownNoise(u32 seed) : rng(seed) {}
			real Next();
			Rng rng;
			real value = 0.0f;
		};

		struct Oscillator
		{
			// Each advances the phase by freq / sampleRate
			real Sine(real freq);
			real Saw(real freq);
			real Square(real freq, real pulseWidth = 0.5f);
			real Triangle(real freq);

			real phase = 0.0f; // [0, 1)
			u32 sampleRate = SAMPLE_RATE;

		private:
			real Advance(real freq);
		};

		// RBJ cookbook biquad. Coefficients can be changed every sample (for sweeps).
		struct Biquad
		{
			enum class Type
			{
				LOWPASS,
				HIGHPASS,
				BANDPASS, // Constant 0 dB peak gain
				PEAK,
				NOTCH
			};

			Biquad() = default;
			Biquad(Type type, real freq, real q, real gainDb = 0.0f, u32 sampleRate = SAMPLE_RATE);

			void Set(Type type, real freq, real q, real gainDb = 0.0f, u32 sampleRate = SAMPLE_RATE);
			real Process(real x);
			void Reset();

			real b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
			real x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
		};

		struct OnePoleLowpass
		{
			void SetCutoff(real freq, u32 sampleRate = SAMPLE_RATE);
			real Process(real x) { value += coeff * (x - value); return value; }
			real coeff = 1.0f;
			real value = 0.0f;
		};

		// Envelopes (t in seconds). All return [0, 1].
		// Linear attack then exponential decay with time constant decay (reaches ~5% after 3 * decay)
		real EnvAttackDecay(real t, real attack, real decay);
		// Attack, hold at 1, then linear release
		real EnvAttackHoldRelease(real t, real attack, real hold, real release);
		// Smooth bump: 0 at t = 0 & t = length, 1 at t = peak
		real EnvBump(real t, real peak, real length);

		// Exponential (perceptually linear) interpolation from f0 to f1 over duration, clamped
		real ExpSweep(real t, real duration, real f0, real f1);

		real Lerp(real a, real b, real t);
		real Clamp01(real x);
		real SmoothStep(real edge0, real edge1, real x);
		real DbToGain(real db);

		// Buffer operations
		void Mix(Buffer& dst, const Buffer& src, real gain = 1.0f, sec offset = 0.0f);
		// Mixes src into dst at offset, wrapping around dst's end (for building seamless loops from events)
		void MixWrapped(Buffer& dst, const Buffer& src, real gain, sec offset);
		void ApplyGain(Buffer& buffer, real gain);
		// Scales so the absolute peak is targetPeak (no-op on silence)
		void Normalize(Buffer& buffer, real targetPeak);
		void FadeIn(Buffer& buffer, sec duration);
		void FadeOut(Buffer& buffer, sec duration);
		// tanh saturation, keeping the peak level about the same
		void SoftClip(Buffer& buffer, real drive);
		void RemoveDC(Buffer& buffer);
		// Turns a buffer generated crossfade seconds longer than the loop into a seamless loop: the extra tail is
		// equal-power crossfaded into the start, then dropped
		void MakeSeamlessLoop(Buffer& buffer, sec crossfade);

		struct Stats
		{
			real peak = 0.0f;
			real rms = 0.0f;
			real peakDb = -120.0f;
			real rmsDb = -120.0f;
		};
		Stats ComputeStats(const Buffer& buffer);

		// Converts to 16 bit PCM (clamped)
		void ToPCM16(const Buffer& buffer, std::vector<i16>& outSamples);
		// Writes a mono 16 bit WAV file
		bool WriteWav(const char* filePath, const Buffer& buffer);
	} // namespace synth
} // namespace flex
