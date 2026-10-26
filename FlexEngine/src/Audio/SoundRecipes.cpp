#include "stdafx.hpp"

#include "Audio/SoundRecipes.hpp"

namespace flex
{
	namespace sound_recipes
	{
		using namespace synth;
		using BQ = Biquad::Type;

		namespace
		{
			const SoundDesc s_Descs[] =
			{
				{ "footstep_grass", 6, false, false },
				{ "footstep_gravel", 6, false, false },
				{ "footstep_rock", 6, false, false },
				{ "footstep_snow", 6, false, false },
				{ "footstep_wood", 6, false, false },
				{ "footstep_water", 6, false, false },
				{ "jump", 3, false, false },
				{ "land_light", 3, false, false },
				{ "land_heavy", 2, false, false },
				{ "glider_stow", 2, false, false },
				{ "place", 3, false, false },
				{ "torch_ignite", 2, false, true },
				{ "campfire_ignite", 1, false, true },
				{ "chop", 4, false, true },
				{ "tree_fall", 2, false, true },
				{ "ui_tick", 1, false, false },
				{ "ui_confirm", 1, false, false },
				{ "loop_engine", 1, true, false },
				{ "loop_skid", 1, true, false },
				{ "loop_wind", 1, true, false },
				{ "loop_air_rush", 1, true, false },
				{ "loop_crickets", 1, true, false },
				{ "loop_campfire", 1, true, true },
			};
			static_assert(sizeof(s_Descs) / sizeof(s_Descs[0]) == (u32)Sound::_COUNT, "s_Descs must match the Sound enum");

			const real ONE_SHOT_PEAK = 0.89f; // -1 dBFS
			const real LOOP_PEAK = 0.7f;

			// Runs a filter over a periodic buffer twice so its state at the start matches the end (keeps loops seamless)
			void FilterCircular(Buffer& buffer, Biquad filter)
			{
				for (real s : buffer.samples)
				{
					filter.Process(s);
				}
				for (real& s : buffer.samples)
				{
					s = filter.Process(s);
				}
			}

			void FilterInPlace(Buffer& buffer, Biquad filter)
			{
				for (real& s : buffer.samples)
				{
					s = filter.Process(s);
				}
			}

			// Low "body" of a foot/object hitting the ground: a pitch-dropping sine plus low-passed noise
			void AddThump(Buffer& dst, Rng& rng, sec start, real gain, real freq, sec decay)
			{
				Oscillator osc;
				WhiteNoise noise(rng.NextU32());
				Biquad lowpass(BQ::LOWPASS, freq * 2.5f, 0.7f);
				const u32 begin = dst.IndexOf(start);
				const u32 count = dst.IndexOf(decay * 6.0f);
				for (u32 i = 0; i < count && begin + i < dst.Count(); ++i)
				{
					const real t = (real)i / (real)dst.sampleRate;
					const real f = freq * (1.0f + 0.8f * std::exp(-t / 0.012f));
					const real env = EnvAttackDecay(t, 0.0015f, decay);
					const real sine = osc.Sine(f) * env;
					const real body = lowpass.Process(noise.Next()) * EnvAttackDecay(t, 0.001f, decay * 0.6f);
					dst.samples[begin + i] += (sine * 0.8f + body * 1.4f) * gain;
				}
			}

			// Sparse random impulses (each a few samples of decaying noise) for crunchy textures. Filter afterwards to
			// colour them. Density & amplitude follow a bump envelope over [start, start + length].
			void AddGrains(Buffer& dst, Rng& rng, sec start, sec length, real rate, sec peak, real gain, real ampPower = 2.0f)
			{
				real t = rng.NextPoissonInterval(rate);
				while (t < length)
				{
					const real env = EnvBump(t, peak, length);
					const real amp = gain * env * std::pow(rng.Next01(), ampPower) * (rng.Next01() < 0.5f ? -1.0f : 1.0f);
					const u32 begin = dst.IndexOf(start + t);
					const u32 grainLength = (u32)rng.RangeInt(2, 14);
					for (u32 i = 0; i < grainLength && begin + i < dst.Count(); ++i)
					{
						dst.samples[begin + i] += amp * rng.NextSigned() * std::exp(-(real)i / ((real)grainLength * 0.4f));
					}
					// Denser near the peak
					t += rng.NextPoissonInterval(rate * (0.25f + env));
				}
			}

			// Short click (high passed noise burst)
			void AddClick(Buffer& dst, Rng& rng, sec start, real gain, real highpassFreq, sec decay)
			{
				WhiteNoise noise(rng.NextU32());
				Biquad highpass(BQ::HIGHPASS, highpassFreq, 0.7f);
				const u32 begin = dst.IndexOf(start);
				const u32 count = dst.IndexOf(decay * 7.0f);
				for (u32 i = 0; i < count && begin + i < dst.Count(); ++i)
				{
					const real t = (real)i / (real)dst.sampleRate;
					dst.samples[begin + i] += highpass.Process(noise.Next()) * EnvAttackDecay(t, 0.0002f, decay) * gain;
				}
			}

			// Damped resonant modes (wood, rock knocks)
			void AddModes(Buffer& dst, sec start, real gain, const real* freqs, const real* gains, const real* decays, u32 modeCount)
			{
				const u32 begin = dst.IndexOf(start);
				for (u32 m = 0; m < modeCount; ++m)
				{
					Oscillator osc;
					const u32 count = dst.IndexOf(decays[m] * 7.0f);
					for (u32 i = 0; i < count && begin + i < dst.Count(); ++i)
					{
						const real t = (real)i / (real)dst.sampleRate;
						dst.samples[begin + i] += osc.Sine(freqs[m]) * EnvAttackDecay(t, 0.0008f, decays[m]) * gains[m] * gain;
					}
				}
			}

			// A pop/crackle: short band-passed noise burst
			void AddPop(Buffer& dst, Rng& rng, sec start, real gain, bool bWrap)
			{
				const real length = rng.Range(0.004f, 0.025f);
				Buffer pop(length + 0.01f, dst.sampleRate);
				WhiteNoise noise(rng.NextU32());
				Biquad bandpass(BQ::BANDPASS, rng.Range(1500.0f, 6000.0f), rng.Range(1.0f, 3.5f));
				Biquad body(BQ::BANDPASS, rng.Range(250.0f, 600.0f), 2.0f);
				const bool bSnap = rng.Next01() < 0.12f;
				for (u32 i = 0; i < pop.Count(); ++i)
				{
					const real t = (real)i / (real)pop.sampleRate;
					const real n = noise.Next();
					const real env = EnvAttackDecay(t, 0.0003f, length * 0.3f);
					pop.samples[i] = (bandpass.Process(n) * 1.5f + (bSnap ? body.Process(n) * 2.0f : 0.0f)) * env;
				}
				const real amp = gain * (bSnap ? 1.0f : std::pow(rng.Range(0.1f, 0.7f), 1.5f));
				if (bWrap)
				{
					MixWrapped(dst, pop, amp, start);
				}
				else
				{
					Mix(dst, pop, amp, start);
				}
			}

			void Finish(Buffer& buffer, real peak, sec fadeOut = 0.01f)
			{
				RemoveDC(buffer);
				FadeIn(buffer, 0.0005f);
				FadeOut(buffer, fadeOut);
				Normalize(buffer, peak);
			}

			Buffer Footstep(Surface surface, u32 variant)
			{
				Rng rng(HashSeed("footstep", (u32)surface * 64 + variant));
				Buffer out(0.34f);

				// Heel then toe
				const sec heel = rng.Range(0.002f, 0.006f);
				const sec toe = heel + rng.Range(0.035f, 0.065f);
				const real toeGain = rng.Range(0.4f, 0.7f);
				const real thumpFreq = rng.Range(65.0f, 95.0f);

				switch (surface)
				{
				case Surface::GRASS:
				{
					AddThump(out, rng, heel, 0.35f, thumpFreq, 0.018f);
					AddThump(out, rng, toe, 0.2f, thumpFreq * 1.1f, 0.014f);
					// Blades brushing & crushing: dense fine grains, high passed
					Buffer rustle(out.Length());
					AddGrains(rustle, rng, heel, rng.Range(0.12f, 0.17f), 2600.0f, 0.03f, 0.9f, 1.5f);
					AddGrains(rustle, rng, toe, rng.Range(0.1f, 0.14f), 2000.0f, 0.025f, 0.6f, 1.5f);
					PinkNoise pink(rng.NextU32());
					for (u32 i = 0; i < rustle.Count(); ++i)
					{
						const real t = (real)i / (real)rustle.sampleRate;
						rustle.samples[i] += pink.Next() * (EnvBump(t - heel, 0.025f, 0.13f) + toeGain * EnvBump(t - toe, 0.02f, 0.1f)) * 0.5f;
					}
					FilterInPlace(rustle, Biquad(BQ::BANDPASS, rng.Range(1900.0f, 2900.0f), 0.8f));
					FilterInPlace(rustle, Biquad(BQ::HIGHPASS, 900.0f, 0.7f));
					Mix(out, rustle, 1.6f);
				} break;
				case Surface::GRAVEL:
				{
					AddThump(out, rng, heel, 0.5f, thumpFreq, 0.02f);
					AddThump(out, rng, toe, 0.3f, thumpFreq * 1.1f, 0.016f);
					// Stones grinding: coarse grains through two resonances
					Buffer crunch(out.Length());
					AddGrains(crunch, rng, heel, rng.Range(0.1f, 0.14f), 1800.0f, 0.02f, 1.0f, 2.5f);
					AddGrains(crunch, rng, toe, rng.Range(0.08f, 0.12f), 1400.0f, 0.02f, toeGain, 2.5f);
					Buffer high = crunch;
					FilterInPlace(crunch, Biquad(BQ::BANDPASS, rng.Range(800.0f, 1100.0f), 1.2f));
					FilterInPlace(high, Biquad(BQ::BANDPASS, rng.Range(2200.0f, 3200.0f), 0.9f));
					Mix(out, crunch, 2.2f);
					Mix(out, high, 1.6f);
				} break;
				case Surface::ROCK:
				{
					AddThump(out, rng, heel, 0.6f, thumpFreq * 1.2f, 0.012f);
					AddThump(out, rng, toe, 0.35f, thumpFreq * 1.3f, 0.01f);
					// Hard sole click & a little resonance
					AddClick(out, rng, heel, 0.18f, 2000.0f, 0.003f);
					AddClick(out, rng, toe, 0.12f * toeGain, 2500.0f, 0.0025f);
					const real base = rng.Range(550.0f, 800.0f);
					const real freqs[] = { base, base * 2.32f, base * 4.1f };
					const real gains[] = { 0.12f, 0.07f, 0.04f };
					const real decays[] = { 0.012f, 0.008f, 0.005f };
					AddModes(out, heel, 1.0f, freqs, gains, decays, 3);
					// Grit scuffing on the stone
					Buffer scuff(out.Length());
					AddGrains(scuff, rng, toe, 0.06f, 900.0f, 0.015f, 0.5f, 3.0f);
					FilterInPlace(scuff, Biquad(BQ::BANDPASS, 2400.0f, 1.0f));
					Mix(out, scuff, 1.2f);
				} break;
				case Surface::SNOW:
				{
					AddThump(out, rng, heel, 0.25f, thumpFreq * 0.9f, 0.025f);
					// Compressing snow: a slower, squeaky crunch
					Buffer crunch(out.Length());
					const sec length = rng.Range(0.16f, 0.22f);
					AddGrains(crunch, rng, heel, length, 1100.0f, length * 0.35f, 1.0f, 1.8f);
					Buffer squeak = crunch;
					FilterInPlace(crunch, Biquad(BQ::BANDPASS, rng.Range(900.0f, 1300.0f), 1.4f));
					FilterInPlace(squeak, Biquad(BQ::BANDPASS, rng.Range(2600.0f, 3400.0f), 2.5f));
					FilterInPlace(crunch, Biquad(BQ::LOWPASS, 5000.0f, 0.7f));
					Mix(out, crunch, 2.4f);
					Mix(out, squeak, 1.4f);
				} break;
				case Surface::WOOD:
				{
					AddThump(out, rng, heel, 0.6f, thumpFreq, 0.02f);
					AddThump(out, rng, toe, 0.35f, thumpFreq * 1.05f, 0.016f);
					// Hollow plank modes
					const real base = rng.Range(115.0f, 160.0f);
					const real freqs[] = { base, base * 2.76f, base * 5.4f, base * 8.9f };
					const real gains[] = { 0.5f, 0.3f, 0.14f, 0.06f };
					const real decays[] = { 0.05f, 0.03f, 0.014f, 0.007f };
					AddModes(out, heel, 1.0f, freqs, gains, decays, 4);
					AddModes(out, toe, toeGain * 0.6f, freqs, gains, decays, 4);
					AddClick(out, rng, heel, 0.12f, 2000.0f, 0.002f);
				} break;
				case Surface::WATER:
				default:
				{
					AddThump(out, rng, heel, 0.2f, thumpFreq, 0.02f);
					// Splash: noise through a rising band pass
					WhiteNoise noise(rng.NextU32());
					Biquad bandpass;
					const real f0 = rng.Range(600.0f, 900.0f);
					const real f1 = rng.Range(2200.0f, 3200.0f);
					for (u32 i = 0; i < out.Count(); ++i)
					{
						const real t = (real)i / (real)out.sampleRate;
						bandpass.Set(BQ::BANDPASS, ExpSweep(t - heel, 0.08f, f0, f1), 1.1f);
						const real env = EnvAttackDecay(t - heel, 0.01f, 0.05f) + 0.5f * EnvAttackDecay(t - toe, 0.008f, 0.04f);
						out.samples[i] += bandpass.Process(noise.Next()) * env * 0.9f;
					}
					// Bubbles: short upward chirps
					const i32 bubbleCount = rng.RangeInt(3, 6);
					for (i32 b = 0; b < bubbleCount; ++b)
					{
						Oscillator osc;
						const sec start = heel + rng.Range(0.01f, 0.15f);
						const real bf0 = rng.Range(500.0f, 1300.0f);
						const real gain = rng.Range(0.08f, 0.2f);
						const u32 begin = out.IndexOf(start);
						const u32 count = out.IndexOf(0.04f);
						for (u32 i = 0; i < count && begin + i < out.Count(); ++i)
						{
							const real t = (real)i / (real)out.sampleRate;
							out.samples[begin + i] += osc.Sine(ExpSweep(t, 0.03f, bf0, bf0 * 1.9f)) * EnvAttackDecay(t, 0.001f, 0.008f) * gain;
						}
					}
				} break;
				}

				// Soften the very top end (shoes on the ground, heard from head height)
				FilterInPlace(out, Biquad(BQ::LOWPASS, 6000.0f, 0.6f));
				Finish(out, ONE_SHOT_PEAK * rng.Range(0.85f, 1.0f), 0.04f);
				return out;
			}

			Buffer Jump(u32 variant)
			{
				Rng rng(HashSeed("jump", variant));
				Buffer out(0.36f);
				// Push off: a scuff & thump
				AddThump(out, rng, 0.0f, 0.5f, rng.Range(75.0f, 95.0f), 0.015f);
				Buffer scuff(out.Length());
				AddGrains(scuff, rng, 0.0f, 0.07f, 1500.0f, 0.01f, 0.8f, 2.0f);
				FilterInPlace(scuff, Biquad(BQ::BANDPASS, 2000.0f, 0.8f));
				Mix(out, scuff, 1.2f);
				// Clothes whooshing as the body rises
				PinkNoise pink(rng.NextU32());
				Biquad bandpass;
				const real f0 = rng.Range(350.0f, 500.0f);
				const real f1 = rng.Range(1500.0f, 2200.0f);
				const sec length = rng.Range(0.24f, 0.3f);
				for (u32 i = 0; i < out.Count(); ++i)
				{
					const real t = (real)i / (real)out.sampleRate;
					bandpass.Set(BQ::BANDPASS, ExpSweep(t, length * 0.7f, f0, f1), 1.2f);
					out.samples[i] += bandpass.Process(pink.Next()) * EnvBump(t, length * 0.3f, length) * 2.2f;
				}
				Finish(out, ONE_SHOT_PEAK, 0.04f);
				return out;
			}

			Buffer Land(bool bHeavy, u32 variant)
			{
				Rng rng(HashSeed(bHeavy ? "land_heavy" : "land_light", variant));
				Buffer out(bHeavy ? 0.85f : 0.42f);
				const real weight = bHeavy ? 1.0f : 0.6f;

				// Both feet, slightly apart
				const sec second = rng.Range(0.012f, 0.03f);
				AddThump(out, rng, 0.0f, 1.0f, rng.Range(55.0f, 70.0f), bHeavy ? 0.06f : 0.03f);
				AddThump(out, rng, second, 0.7f, rng.Range(60.0f, 80.0f), bHeavy ? 0.05f : 0.025f);

				// Low rumble of the impact
				BrownNoise brown(rng.NextU32());
				Biquad lowpass(BQ::LOWPASS, bHeavy ? 220.0f : 300.0f, 0.8f);
				for (u32 i = 0; i < out.Count(); ++i)
				{
					const real t = (real)i / (real)out.sampleRate;
					out.samples[i] += lowpass.Process(brown.Next()) * EnvAttackDecay(t, 0.002f, bHeavy ? 0.09f : 0.04f) * 1.6f * weight;
				}

				// Debris kicked up & settling
				Buffer debris(out.Length());
				AddGrains(debris, rng, 0.0f, bHeavy ? 0.3f : 0.14f, 2200.0f, 0.02f, 1.0f, 2.2f);
				if (bHeavy)
				{
					AddGrains(debris, rng, 0.15f, 0.55f, 120.0f, 0.1f, 0.6f, 2.0f);
				}
				Buffer debrisHigh = debris;
				FilterInPlace(debris, Biquad(BQ::BANDPASS, 900.0f, 1.0f));
				FilterInPlace(debrisHigh, Biquad(BQ::BANDPASS, 2800.0f, 0.9f));
				Mix(out, debris, 1.6f * weight);
				Mix(out, debrisHigh, 1.0f * weight);

				if (bHeavy)
				{
					SoftClip(out, 1.6f);
				}
				Finish(out, ONE_SHOT_PEAK, 0.08f);
				return out;
			}

			Buffer GliderStow(u32 variant)
			{
				Rng rng(HashSeed("glider_stow", variant));
				Buffer out(0.8f);
				const sec flapLength = rng.Range(0.5f, 0.58f);

				// Canopy fabric fluttering as it collapses: the flutter rate slows as it folds
				PinkNoise pink(rng.NextU32());
				WhiteNoise white(rng.NextU32());
				Biquad fabric(BQ::BANDPASS, rng.Range(1000.0f, 1400.0f), 0.7f);
				Biquad whoosh;
				Oscillator flutter;
				for (u32 i = 0; i < out.Count(); ++i)
				{
					const real t = (real)i / (real)out.sampleRate;
					const real flutterRate = ExpSweep(t, flapLength, 26.0f, 7.0f);
					const real s = flutter.Sine(flutterRate) * 0.5f + 0.5f;
					const real am = s * s * s;
					const real env = EnvBump(t, 0.07f, flapLength);
					out.samples[i] += fabric.Process(white.Next()) * am * env * 1.4f;
					whoosh.Set(BQ::BANDPASS, ExpSweep(t, flapLength, 2200.0f, 500.0f), 0.9f);
					out.samples[i] += whoosh.Process(pink.Next()) * env * 1.3f;
				}

				// Bundled up & strapped: a buckle clack, then a quieter one
				const sec clack = flapLength + rng.Range(0.0f, 0.04f);
				for (i32 c = 0; c < 2; ++c)
				{
					const sec start = clack + c * rng.Range(0.05f, 0.08f);
					const real gain = c == 0 ? 1.0f : 0.5f;
					AddClick(out, rng, start, 0.5f * gain, 2500.0f, 0.002f);
					const real base = rng.Range(1600.0f, 2100.0f);
					const real freqs[] = { base, base * 2.6f };
					const real gains[] = { 0.25f, 0.12f };
					const real decays[] = { 0.008f, 0.004f };
					AddModes(out, start, gain, freqs, gains, decays, 2);
				}
				Finish(out, ONE_SHOT_PEAK * 0.9f, 0.05f);
				return out;
			}

			Buffer Place(u32 variant)
			{
				Rng rng(HashSeed("place", variant));
				Buffer out(0.3f);
				AddThump(out, rng, 0.0f, 1.0f, rng.Range(90.0f, 120.0f), 0.03f);
				AddClick(out, rng, 0.0f, 0.15f, 3000.0f, 0.002f);
				const real base = rng.Range(220.0f, 300.0f);
				const real freqs[] = { base, base * 2.7f };
				const real gains[] = { 0.25f, 0.1f };
				const real decays[] = { 0.03f, 0.015f };
				AddModes(out, 0.0f, 1.0f, freqs, gains, decays, 2);
				Buffer grit(out.Length());
				AddGrains(grit, rng, 0.005f, 0.08f, 1200.0f, 0.015f, 0.6f, 2.5f);
				FilterInPlace(grit, Biquad(BQ::BANDPASS, 1800.0f, 1.0f));
				Mix(out, grit, 1.0f);
				Finish(out, ONE_SHOT_PEAK, 0.05f);
				return out;
			}

			// An axe biting into wood: a sharp click, a hollow knock from the trunk & a few splinters
			Buffer Chop(u32 variant)
			{
				Rng rng(HashSeed("chop", variant));
				Buffer out(0.35f);
				AddClick(out, rng, 0.0f, 0.5f, 2500.0f, 0.004f);
				AddThump(out, rng, 0.0f, 0.6f, rng.Range(140.0f, 180.0f), 0.05f);
				const real base = rng.Range(380.0f, 520.0f);
				const real freqs[] = { base, base * 1.9f, base * 3.1f };
				const real gains[] = { 0.45f, 0.2f, 0.08f };
				const real decays[] = { 0.06f, 0.035f, 0.02f };
				AddModes(out, 0.0f, 1.0f, freqs, gains, decays, 3);
				Buffer splinters(out.Length());
				AddGrains(splinters, rng, 0.01f, 0.12f, 250.0f, 0.004f, 0.5f, 2.0f);
				FilterInPlace(splinters, Biquad(BQ::BANDPASS, rng.Range(2800.0f, 3600.0f), 1.2f));
				Mix(out, splinters, 1.0f);
				Finish(out, ONE_SHOT_PEAK, 0.05f);
				return out;
			}

			// A tree hitting the ground: a deep impact, rolling rumble & a long rustle of branches & leaves
			Buffer TreeFall(u32 variant)
			{
				Rng rng(HashSeed("tree_fall", variant));
				Buffer out(1.6f);
				AddThump(out, rng, 0.0f, 1.0f, rng.Range(45.0f, 60.0f), 0.25f);
				AddThump(out, rng, rng.Range(0.12f, 0.2f), 0.5f, rng.Range(70.0f, 90.0f), 0.15f);
				const real freqs[] = { rng.Range(110.0f, 140.0f), rng.Range(230.0f, 280.0f) };
				const real gains[] = { 0.35f, 0.15f };
				const real decays[] = { 0.2f, 0.1f };
				AddModes(out, 0.0f, 1.0f, freqs, gains, decays, 2);
				Buffer branches(out.Length());
				AddGrains(branches, rng, 0.0f, 0.5f, 600.0f, 0.006f, 0.45f, 2.0f);
				FilterInPlace(branches, Biquad(BQ::BANDPASS, 1500.0f, 0.8f));
				Mix(out, branches, 1.0f);
				Buffer leaves(out.Length());
				AddGrains(leaves, rng, 0.05f, 1.3f, 1800.0f, 0.01f, 0.18f, 3.0f);
				FilterInPlace(leaves, Biquad(BQ::BANDPASS, 3500.0f, 0.7f));
				FilterInPlace(leaves, Biquad(BQ::HIGHPASS, 1200.0f, 0.7f));
				Mix(out, leaves, 1.0f);
				Finish(out, ONE_SHOT_PEAK, 0.3f);
				return out;
			}

			// A flame catching: low passed noise opening up & closing again ("fwoomp")
			void AddFlare(Buffer& dst, Rng& rng, sec start, sec length, sec peak, real maxCutoff, real gain)
			{
				WhiteNoise white(rng.NextU32());
				BrownNoise brown(rng.NextU32());
				Biquad lowpass;
				Biquad body(BQ::LOWPASS, 160.0f, 0.7f);
				const u32 begin = dst.IndexOf(start);
				const u32 count = dst.IndexOf(length);
				for (u32 i = 0; i < count && begin + i < dst.Count(); ++i)
				{
					const real t = (real)i / (real)dst.sampleRate;
					const real env = EnvBump(t, peak, length);
					lowpass.Set(BQ::LOWPASS, 180.0f + maxCutoff * env * env, 0.9f);
					dst.samples[begin + i] += (lowpass.Process(white.Next()) * 0.8f + body.Process(brown.Next()) * 1.2f) * std::pow(env, 1.3f) * gain;
				}
			}

			Buffer TorchIgnite(u32 variant)
			{
				Rng rng(HashSeed("torch_ignite", variant));
				Buffer out(0.95f);
				// Strike scratch
				Buffer scratch(out.Length());
				AddGrains(scratch, rng, 0.0f, 0.06f, 4000.0f, 0.01f, 0.8f, 1.5f);
				FilterInPlace(scratch, Biquad(BQ::HIGHPASS, 2500.0f, 0.7f));
				Mix(out, scratch, 0.8f);
				AddFlare(out, rng, 0.03f, 0.85f, 0.12f, 3500.0f, 1.0f);
				real t = 0.15f;
				while (t < 0.9f)
				{
					AddPop(out, rng, t, 0.35f, false);
					t += rng.NextPoissonInterval(14.0f);
				}
				Finish(out, ONE_SHOT_PEAK, 0.1f);
				return out;
			}

			Buffer CampfireIgnite(u32 variant)
			{
				Rng rng(HashSeed("campfire_ignite", variant));
				Buffer out(1.9f);
				// Match strike & fizz
				Buffer strike(out.Length());
				AddGrains(strike, rng, 0.0f, 0.1f, 5000.0f, 0.02f, 0.9f, 1.5f);
				FilterInPlace(strike, Biquad(BQ::HIGHPASS, 2500.0f, 0.7f));
				Mix(out, strike, 0.9f);
				{
					WhiteNoise noise(rng.NextU32());
					Biquad fizz(BQ::BANDPASS, 3200.0f, 1.5f);
					for (u32 i = 0; i < out.Count(); ++i)
					{
						const real tt = (real)i / (real)out.sampleRate;
						out.samples[i] += fizz.Process(noise.Next()) * EnvAttackDecay(tt - 0.08f, 0.01f, 0.12f) * 0.4f;
					}
				}
				// Kindling catches, then the fire roars up
				AddFlare(out, rng, 0.25f, 0.6f, 0.15f, 1800.0f, 0.4f);
				AddFlare(out, rng, 0.5f, 1.4f, 0.75f, 2600.0f, 1.0f);
				// Crackles become more frequent
				real t = 0.3f;
				while (t < 1.85f)
				{
					AddPop(out, rng, t, 0.45f, false);
					t += rng.NextPoissonInterval(Lerp(4.0f, 16.0f, Clamp01((t - 0.3f) / 1.0f)));
				}
				Finish(out, ONE_SHOT_PEAK, 0.25f);
				return out;
			}

			Buffer UITick()
			{
				Rng rng(HashSeed("ui_tick", 0));
				Buffer out(0.06f);
				Oscillator high;
				Oscillator low;
				for (u32 i = 0; i < out.Count(); ++i)
				{
					const real t = (real)i / (real)out.sampleRate;
					out.samples[i] = high.Sine(2600.0f) * EnvAttackDecay(t, 0.0005f, 0.006f) + low.Sine(1300.0f) * EnvAttackDecay(t, 0.0008f, 0.012f) * 0.5f;
				}
				AddClick(out, rng, 0.0f, 0.3f, 4000.0f, 0.001f);
				Finish(out, ONE_SHOT_PEAK * 0.8f, 0.01f);
				return out;
			}

			Buffer UIConfirm()
			{
				Buffer out(0.26f);
				const real notes[] = { 1318.51f, 1760.0f }; // E6, A6
				const sec starts[] = { 0.0f, 0.07f };
				for (u32 n = 0; n < 2; ++n)
				{
					Oscillator sine;
					Oscillator tri;
					const u32 begin = out.IndexOf(starts[n]);
					for (u32 i = 0; begin + i < out.Count(); ++i)
					{
						const real t = (real)i / (real)out.sampleRate;
						out.samples[begin + i] += (sine.Sine(notes[n]) + tri.Triangle(notes[n] * 2.0f) * 0.15f) * EnvAttackDecay(t, 0.003f, 0.05f) * (n == 0 ? 0.7f : 1.0f);
					}
				}
				Finish(out, ONE_SHOT_PEAK * 0.8f, 0.03f);
				return out;
			}

			// Exactly one second long, & every component is periodic within it, so it loops seamlessly. The firing
			// rate (50 Hz) is scaled at runtime with the source's pitch.
			Buffer EngineLoop()
			{
				Rng rng(HashSeed("loop_engine", 0));
				Buffer out(1.0f);
				const u32 firingRate = 50;
				const real cycle = 1.0f / (real)firingRate;

				// One combustion pulse per firing: two damped resonances & a burst of exhaust noise
				Buffer pulse(cycle * 3.0f);
				{
					Oscillator a;
					Oscillator b;
					WhiteNoise noise(rng.NextU32());
					Biquad lowpass(BQ::LOWPASS, 1100.0f, 0.8f);
					for (u32 i = 0; i < pulse.Count(); ++i)
					{
						const real t = (real)i / (real)pulse.sampleRate;
						pulse.samples[i] = a.Sine(155.0f) * EnvAttackDecay(t, 0.0008f, 0.006f) +
							b.Sine(92.0f) * EnvAttackDecay(t, 0.001f, 0.01f) * 0.8f +
							lowpass.Process(noise.Next()) * EnvAttackDecay(t, 0.0005f, 0.004f) * 1.6f;
					}
				}
				for (u32 k = 0; k < firingRate; ++k)
				{
					// Uneven cylinders give it a lumpy idle (still periodic, the pattern repeats every 4 firings)
					static const real cylinderGains[] = { 1.0f, 0.82f, 0.93f, 0.76f };
					const real jitter = rng.Range(-0.0006f, 0.0006f);
					MixWrapped(out, pulse, cylinderGains[k % 4] * rng.Range(0.92f, 1.0f), k * cycle + jitter);
				}
				// Sub rumble at half the firing rate (four stroke)
				Oscillator sub;
				for (u32 i = 0; i < out.Count(); ++i)
				{
					out.samples[i] += sub.Sine((real)firingRate * 0.5f) * 0.25f;
				}
				FilterCircular(out, Biquad(BQ::LOWPASS, 2400.0f, 0.7f));
				FilterCircular(out, Biquad(BQ::PEAK, 180.0f, 1.0f, 4.0f));
				Normalize(out, 1.0f);
				SoftClip(out, 1.8f);
				Normalize(out, LOOP_PEAK);
				return out;
			}

			Buffer SkidLoop()
			{
				Rng rng(HashSeed("loop_skid", 0));
				const sec loopLength = 1.0f;
				const sec crossfade = 0.15f;
				Buffer noiseBed(loopLength + crossfade);
				{
					WhiteNoise noise(rng.NextU32());
					Biquad bandpass(BQ::BANDPASS, 1800.0f, 2.5f);
					Biquad bandpass2(BQ::BANDPASS, 900.0f, 1.5f);
					for (real& s : noiseBed.samples)
					{
						const real n = noise.Next();
						s = bandpass.Process(n) * 0.5f + bandpass2.Process(n) * 0.25f;
					}
					MakeSeamlessLoop(noiseBed, crossfade);
				}

				// Tyre squeal: an FM'd tone. Integer frequencies over a one second loop keep it periodic.
				Buffer out(loopLength);
				const real carrier = 1150.0f;
				const real vibratoRate = 9.0f;
				const real vibratoDepth = 60.0f;
				for (u32 i = 0; i < out.Count(); ++i)
				{
					const real t = (real)i / (real)out.sampleRate;
					const real phase = SYNTH_TWO_PI * carrier * t + (vibratoDepth / vibratoRate) * std::sin(SYNTH_TWO_PI * vibratoRate * t);
					const real flutter = 0.75f + 0.25f * std::sin(SYNTH_TWO_PI * 13.0f * t);
					out.samples[i] = (std::sin(phase) + 0.3f * std::sin(2.0f * phase) + 0.1f * std::sin(3.0f * phase)) * flutter * 0.5f + noiseBed.samples[i];
				}
				SoftClip(out, 1.5f);
				Normalize(out, LOOP_PEAK);
				return out;
			}

			Buffer WindLoop()
			{
				Rng rng(HashSeed("loop_wind", 0));
				const sec loopLength = 12.0f;
				const sec crossfade = 1.0f;
				Buffer out(loopLength + crossfade);
				PinkNoise pink(rng.NextU32());
				BrownNoise brown(rng.NextU32());
				Biquad howl;
				Biquad howl2;
				Biquad rumble(BQ::LOWPASS, 450.0f, 0.7f);
				const real p0 = rng.Range(0.0f, SYNTH_TWO_PI);
				const real p1 = rng.Range(0.0f, SYNTH_TWO_PI);
				const real p2 = rng.Range(0.0f, SYNTH_TWO_PI);
				real gust = 0.0f;
				for (u32 i = 0; i < out.Count(); ++i)
				{
					// Modulating every few samples is plenty (& much cheaper)
					if ((i & 31) == 0)
					{
						const real t = (real)i / (real)out.sampleRate;
						// Modulators complete whole cycles over the loop, so the crossfaded tail lines up with the start
						const real w = SYNTH_TWO_PI * t / loopLength;
						const real centre = 340.0f * std::pow(2.0f, 0.8f * std::sin(w + p0) + 0.35f * std::sin(3.0f * w + p1));
						gust = 0.5f + 0.5f * (0.5f + 0.5f * std::sin(2.0f * w + p2)) * (0.7f + 0.3f * std::sin(5.0f * w + p0));
						howl.Set(BQ::BANDPASS, centre, 6.0f);
						howl2.Set(BQ::BANDPASS, centre * 2.3f, 9.0f);
					}
					const real n = pink.Next();
					out.samples[i] = (howl.Process(n) * 2.2f + howl2.Process(n) * 1.1f) * gust * gust + rumble.Process(brown.Next()) * 0.6f * gust;
				}
				MakeSeamlessLoop(out, crossfade);
				Normalize(out, LOOP_PEAK);
				return out;
			}

			Buffer AirRushLoop()
			{
				Rng rng(HashSeed("loop_air_rush", 0));
				const sec loopLength = 4.0f;
				const sec crossfade = 0.5f;
				Buffer out(loopLength + crossfade);
				WhiteNoise white(rng.NextU32());
				PinkNoise pink(rng.NextU32());
				Biquad bandpass(BQ::BANDPASS, 1400.0f, 0.5f);
				Biquad lowpass(BQ::LOWPASS, 700.0f, 0.7f);
				for (u32 i = 0; i < out.Count(); ++i)
				{
					const real t = (real)i / (real)out.sampleRate;
					const real w = SYNTH_TWO_PI * t / loopLength;
					const real buffet = 0.8f + 0.2f * std::sin(3.0f * w) * std::sin(7.0f * w);
					out.samples[i] = (bandpass.Process(white.Next()) * 0.7f + lowpass.Process(pink.Next()) * 1.2f) * buffet;
				}
				MakeSeamlessLoop(out, crossfade);
				Normalize(out, LOOP_PEAK);
				return out;
			}

			Buffer CricketsLoop()
			{
				Rng rng(HashSeed("loop_crickets", 0));
				const sec loopLength = 8.0f;
				Buffer out(loopLength);

				// Distant chorus: narrow band noise pulsing (18 Hz: whole cycles over the loop)
				{
					const sec crossfade = 0.5f;
					Buffer chorus(loopLength + crossfade);
					WhiteNoise noise(rng.NextU32());
					Biquad bandpass(BQ::BANDPASS, 4700.0f, 8.0f);
					for (u32 i = 0; i < chorus.Count(); ++i)
					{
						const real t = (real)i / (real)chorus.sampleRate;
						const real s = 0.5f + 0.5f * std::sin(SYNTH_TWO_PI * 18.0f * t);
						chorus.samples[i] = bandpass.Process(noise.Next()) * s * s;
					}
					MakeSeamlessLoop(chorus, crossfade);
					Mix(out, chorus, 0.12f);
				}

				// A few nearby crickets, each chirping regularly at its own pitch & rate
				const i32 cricketCount = 4;
				for (i32 c = 0; c < cricketCount; ++c)
				{
					const real carrier = rng.Range(4200.0f, 5300.0f);
					const real gain = c == cricketCount - 1 ? 0.25f : rng.Range(0.45f, 1.0f);
					const i32 pulsesPerChirp = rng.RangeInt(2, 4);
					const sec pulseLength = rng.Range(0.012f, 0.018f);
					const sec pulseSpacing = pulseLength + rng.Range(0.012f, 0.02f);
					// A whole number of chirps per loop
					const i32 chirpCount = (i32)std::round(loopLength / rng.Range(0.5f, 0.95f));
					const sec chirpPeriod = loopLength / (real)chirpCount;
					const sec offset = rng.Range(0.0f, chirpPeriod);

					Buffer pulse(pulseLength);
					Oscillator osc;
					for (u32 i = 0; i < pulse.Count(); ++i)
					{
						const real t = (real)i / (real)pulse.sampleRate;
						const real s = std::sin(SYNTH_PI * t / pulseLength);
						pulse.samples[i] = osc.Sine(carrier * (1.0f - 0.03f * t / pulseLength)) * s * s;
					}
					for (i32 k = 0; k < chirpCount; ++k)
					{
						const sec chirpStart = offset + k * chirpPeriod + rng.Range(-0.01f, 0.01f);
						const real chirpGain = gain * rng.Range(0.8f, 1.0f);
						for (i32 p = 0; p < pulsesPerChirp; ++p)
						{
							MixWrapped(out, pulse, chirpGain * (p == pulsesPerChirp - 1 ? 0.8f : 1.0f), chirpStart + p * pulseSpacing);
						}
					}
				}
				Normalize(out, LOOP_PEAK);
				return out;
			}

			Buffer CampfireLoop()
			{
				Rng rng(HashSeed("loop_campfire", 0));
				const sec loopLength = 6.0f;
				const sec crossfade = 0.6f;
				Buffer out(loopLength + crossfade);
				// No low rumble layer: it read as a running fan. Only a faint hiss under the crackle.
				rng.NextU32(); // keep the RNG stream (pop timing) unchanged
				PinkNoise pink(rng.NextU32());
				Biquad hiss(BQ::BANDPASS, 2600.0f, 0.6f);
				OnePoleLowpass flicker;
				flicker.SetCutoff(3.0f);
				WhiteNoise flickerNoise(rng.NextU32());
				for (u32 i = 0; i < out.Count(); ++i)
				{
					const real f = 0.6f + 2.5f * flicker.Process(flickerNoise.Next());
					out.samples[i] = hiss.Process(pink.Next()) * 0.12f * f;
				}
				MakeSeamlessLoop(out, crossfade);

				// Pops & snaps (wrapped so the loop stays seamless)
				real t = 0.0f;
				while (t < loopLength)
				{
					AddPop(out, rng, t, 0.9f, true);
					t += rng.NextPoissonInterval(9.0f);
				}
				Normalize(out, LOOP_PEAK);
				return out;
			}
		} // namespace

		const SoundDesc& GetSoundDesc(Sound sound)
		{
			return s_Descs[(u32)sound];
		}

		Buffer Generate(Sound sound, u32 variant)
		{
			switch (sound)
			{
			case Sound::FOOTSTEP_GRASS:
			case Sound::FOOTSTEP_GRAVEL:
			case Sound::FOOTSTEP_ROCK:
			case Sound::FOOTSTEP_SNOW:
			case Sound::FOOTSTEP_WOOD:
			case Sound::FOOTSTEP_WATER:
				return Footstep((Surface)((u32)sound - (u32)Sound::FOOTSTEP_GRASS), variant);
			case Sound::JUMP: return Jump(variant);
			case Sound::LAND_LIGHT: return Land(false, variant);
			case Sound::LAND_HEAVY: return Land(true, variant);
			case Sound::GLIDER_STOW: return GliderStow(variant);
			case Sound::PLACE: return Place(variant);
			case Sound::TORCH_IGNITE: return TorchIgnite(variant);
			case Sound::CAMPFIRE_IGNITE: return CampfireIgnite(variant);
			case Sound::CHOP: return Chop(variant);
			case Sound::TREE_FALL: return TreeFall(variant);
			case Sound::UI_TICK: return UITick();
			case Sound::UI_CONFIRM: return UIConfirm();
			case Sound::LOOP_ENGINE: return EngineLoop();
			case Sound::LOOP_SKID: return SkidLoop();
			case Sound::LOOP_WIND: return WindLoop();
			case Sound::LOOP_AIR_RUSH: return AirRushLoop();
			case Sound::LOOP_CRICKETS: return CricketsLoop();
			case Sound::LOOP_CAMPFIRE: return CampfireLoop();
			default: return Buffer(0.01f);
			}
		}
	} // namespace sound_recipes
} // namespace flex
