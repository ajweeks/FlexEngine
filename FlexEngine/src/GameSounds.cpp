#include "stdafx.hpp"

#include "GameSounds.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btRigidBody.h>
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "FlexEngine.hpp"
#include "Game.hpp"
#include "Helpers.hpp"
#include "Physics/PhysicsTypeConversions.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/JobSystem.hpp"
#include "Traversal.hpp"

namespace flex
{
	namespace GameSounds
	{
		using sound_recipes::Sound;

		namespace
		{
			using Clock = std::chrono::steady_clock;

			struct GeneratedSound
			{
				std::vector<i16> pcm;
				synth::Stats stats;
				real seconds = 0.0f;
			};

			struct SoundBank
			{
				std::vector<AudioSourceID> ids;
				u32 lastIndex = ~0u;
			};

			struct Loop
			{
				Sound sound;
				// Units per second the gain moves towards its target
				real fadeRate;
				real gain = 0.0f;
				real target = 0.0f;
				real pitch = 1.0f;
				bool bPlaying = false;
			};

			JobSystem::Context s_Context;
			bool s_bGenerating = false;
			bool s_bReady = false;
			bool s_bDump = false;
			// FLEX_SOUND_LOG: print every sound played (for testing without listening)
			bool s_bLog = false;
			std::string s_DumpDirectory;
			Clock::time_point s_GenerationStart;
			std::atomic<i64> s_GenerationEndMicros{ 0 };
			std::atomic<i64> s_GenerationCPUMicros{ 0 };
			std::vector<std::vector<GeneratedSound>> s_Generated;

			SoundBank s_Banks[(u32)Sound::_COUNT];
			synth::Rng s_Rng(0x5EED1234u);

			real s_SfxVolume = 1.0f;
			real s_AmbienceVolume = 1.0f;

			Loop s_Engine{ Sound::LOOP_ENGINE, 4.0f };
			Loop s_Skid{ Sound::LOOP_SKID, 5.0f };
			Loop s_Wind{ Sound::LOOP_WIND, 0.4f };
			Loop s_AirRush{ Sound::LOOP_AIR_RUSH, 3.0f }; // Follows speed closely
			Loop s_Crickets{ Sound::LOOP_CRICKETS, 0.3f };
			Loop s_Fire{ Sound::LOOP_CAMPFIRE, 1.5f };
			Loop s_TreasureHum{ Sound::LOOP_TREASURE_HUM, 1.0f };
			Loop* const s_Loops[] = { &s_Engine, &s_Skid, &s_Wind, &s_AirRush, &s_Crickets, &s_Fire, &s_TreasureHum };

			// Reported by other systems during the frame
			bool s_bVehicleReported = false;
			real s_VehicleSpeed = 0.0f;
			real s_VehicleThrottle = 0.0f;
			bool s_bVehicleBraking = false;
			real s_VehicleSlip = 0.0f;
			// Smoothed so momentary wheel spin doesn't make the skid loop flicker on & off
			real s_VehicleSlipSmoothed = 0.0f;

			bool s_bFireReported = false;
			glm::vec3 s_FirePos;
			real s_FireIntensity = 0.0f;
			real s_FireDistSq = 0.0f;

			bool s_bTreasureReported = false;
			glm::vec3 s_TreasurePos;
			real s_TreasureDistSq = 0.0f;

			glm::vec3 s_ListenerPos(0.0f);

			// Player tracking
			bool s_bWasGrounded = true;
			real s_AirTime = 0.0f;
			real s_MinVelY = 0.0f;
			real s_StepPhase = 0.0f;
			real s_PlayerAirSpeed = 0.0f;

			// Ambience
			real s_Gust = 0.5f;
			real s_GustTarget = 0.5f;
			real s_GustTimer = 0.0f;
			real s_AmbienceTimer = 0.0f;
			real s_HeightAboveGround = 10.0f;
			real s_Enclosed = 0.0f;
			real s_Ocean = 0.0f;
			bool s_bHasTerrain = false;

			i64 MicrosSince(Clock::time_point start)
			{
				return (i64)std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
			}

			real MoveTowards(real value, real target, real maxDelta)
			{
				if (std::abs(target - value) <= maxDelta)
				{
					return target;
				}
				return value + (target > value ? maxDelta : -maxDelta);
			}

			void GenerateJob(u32 soundIndex, u32 variant)
			{
				const Clock::time_point start = Clock::now();
				const synth::Buffer buffer = sound_recipes::Generate((Sound)soundIndex, variant);
				GeneratedSound& generated = s_Generated[soundIndex][variant];
				synth::ToPCM16(buffer, generated.pcm);
				generated.stats = synth::ComputeStats(buffer);
				generated.seconds = buffer.Length();
				if (s_bDump)
				{
					const std::string path = s_DumpDirectory + sound_recipes::GetSoundDesc((Sound)soundIndex).name + "_" + std::to_string(variant) + ".wav";
					synth::WriteWav(path.c_str(), buffer);
				}

				s_GenerationCPUMicros += MicrosSince(start);
				const i64 end = MicrosSince(s_GenerationStart);
				i64 prevEnd = s_GenerationEndMicros.load();
				while (prevEnd < end && !s_GenerationEndMicros.compare_exchange_weak(prevEnd, end))
				{
				}
			}

			void Upload()
			{
				const Clock::time_point start = Clock::now();
				u32 soundCount = 0;
				real totalSeconds = 0.0f;
				for (u32 s = 0; s < (u32)Sound::_COUNT; ++s)
				{
					const sound_recipes::SoundDesc& desc = sound_recipes::GetSoundDesc((Sound)s);
					for (u32 v = 0; v < (u32)s_Generated[s].size(); ++v)
					{
						GeneratedSound& generated = s_Generated[s][v];
						const std::string name = std::string("synth_") + desc.name + "_" + std::to_string(v);
						const AudioSourceID id = AudioManager::CreateSourceFromSamples(generated.pcm.data(), (u32)generated.pcm.size(), synth::SAMPLE_RATE, !desc.b3D, name);
						if (id == InvalidAudioSourceID)
						{
							continue;
						}
						if (desc.b3D)
						{
							if ((Sound)s == Sound::LOOP_TREASURE_HUM)
							{
								// Audible from far off, so it can lead the player to the chest
								AudioManager::SetSourceRolloff(id, 5.0f, 300.0f, 0.8f);
							}
							else
							{
								AudioManager::SetSourceRolloff(id, 3.0f, 60.0f);
							}
						}
						s_Banks[s].ids.push_back(id);
						++soundCount;
						totalSeconds += generated.seconds;
						if (s_bDump)
						{
							Print("  %-18s v%u  %.3f s  peak %6.2f dBFS  rms %6.2f dBFS\n", desc.name, v, generated.seconds, generated.stats.peakDb, generated.stats.rmsDb);
						}
					}
				}
				s_Generated.clear();
				s_Generated.shrink_to_fit();

				Print("GameSounds: synthesized %u sounds (%.1f s of audio) in %.1f ms (async, %.1f ms CPU on workers), uploaded in %.2f ms%s\n",
					soundCount, totalSeconds, s_GenerationEndMicros.load() / 1000.0f, s_GenerationCPUMicros.load() / 1000.0f, MicrosSince(start) / 1000.0f,
					s_bDump ? (std::string(", dumped to ") + s_DumpDirectory).c_str() : "");
				s_bGenerating = false;
				s_bReady = true;
			}

			AudioSourceID PickVariant(Sound sound)
			{
				SoundBank& bank = s_Banks[(u32)sound];
				if (bank.ids.empty())
				{
					return InvalidAudioSourceID;
				}
				u32 index = 0;
				if (bank.ids.size() > 1)
				{
					// Never the same one twice in a row
					index = s_Rng.NextU32() % (u32)(bank.ids.size() - 1);
					if (bank.lastIndex < (u32)bank.ids.size() && index >= bank.lastIndex)
					{
						++index;
					}
				}
				bank.lastIndex = index;
				return bank.ids[index];
			}

			void PlayOneShot(Sound sound, real gain, real pitchVariance)
			{
				if (!s_bReady)
				{
					return;
				}
				const AudioSourceID id = PickVariant(sound);
				if (id == InvalidAudioSourceID)
				{
					return;
				}
				AudioManager::SetSourcePitch(id, 1.0f + s_Rng.NextSigned() * pitchVariance);
				AudioManager::PlaySourceWithGain(id, glm::clamp(gain * s_SfxVolume, 0.0f, 1.0f));
				if (s_bLog)
				{
					Print("[sound] %s (gain %.2f)\n", AudioManager::GetSourceFileName(id).c_str(), gain * s_SfxVolume);
				}
			}

			void PlayOneShotAt(Sound sound, const glm::vec3& posWS, real gain, real pitchVariance)
			{
				if (!s_bReady)
				{
					return;
				}
				const AudioSourceID id = PickVariant(sound);
				if (id == InvalidAudioSourceID)
				{
					return;
				}
				AudioManager::SetSourcePositionWS(id, posWS);
				AudioManager::SetSourcePitch(id, 1.0f + s_Rng.NextSigned() * pitchVariance);
				AudioManager::PlaySourceWithGain(id, glm::clamp(gain * s_SfxVolume, 0.0f, 1.0f));
				if (s_bLog)
				{
					Print("[sound] %s at (%.1f, %.1f, %.1f)\n", AudioManager::GetSourceFileName(id).c_str(), posWS.x, posWS.y, posWS.z);
				}
			}

			void UpdateLoop(Loop& loop, real dt)
			{
				SoundBank& bank = s_Banks[(u32)loop.sound];
				if (bank.ids.empty())
				{
					return;
				}
				const AudioSourceID id = bank.ids[0];
				loop.gain = MoveTowards(loop.gain, glm::clamp(loop.target, 0.0f, 1.0f), loop.fadeRate * dt);
				// Only (re)started once clearly audible, so tiny targets don't keep restarting it
				if (loop.gain > 0.001f && (loop.bPlaying || loop.target > 0.02f))
				{
					AudioManager::SetSourceGain(id, loop.gain);
					AudioManager::SetSourcePitch(id, loop.pitch);
					if (!loop.bPlaying)
					{
						AudioManager::SetSourceLooping(id, true);
						// Start somewhere random so long loops don't always begin the same way
						AudioManager::PlaySourceAtOffset(id, s_Rng.Next01() * 0.95f);
						loop.bPlaying = true;
						if (s_bLog)
						{
							Print("[sound] loop start %s (target %.2f, pitch %.2f)\n", AudioManager::GetSourceFileName(id).c_str(), loop.target, loop.pitch);
						}
					}
				}
				else if (loop.bPlaying && loop.gain <= 0.001f)
				{
					AudioManager::StopSource(id);
					loop.bPlaying = false;
					if (s_bLog)
					{
						Print("[sound] loop stop %s\n", AudioManager::GetSourceFileName(id).c_str());
					}
				}
			}

			void UpdateListener()
			{
				BaseCamera* camera = g_CameraManager->CurrentCamera();
				if (camera == nullptr)
				{
					return;
				}
				s_ListenerPos = camera->position;
				AudioManager::SetListenerPos(camera->position);
				AudioManager::SetListenerOrientation(camera->forward, camera->up);
			}

			void ResetPlayerTracking()
			{
				s_bWasGrounded = true;
				s_AirTime = 0.0f;
				s_MinVelY = 0.0f;
				s_StepPhase = 0.7f;
				s_PlayerAirSpeed = 0.0f;
			}

			void UpdatePlayer(real dt, bool bPaused)
			{
				BaseScene* scene = g_SceneManager->CurrentScene();
				Player* player = scene != nullptr ? scene->GetPlayer(0) : nullptr;
				const bool bMenuOpen = g_Game != nullptr && g_Game->IsMenuOpen();
				if (player == nullptr || bPaused || bMenuOpen || player->GetRigidBody() == nullptr ||
					player->GetRidingVehicleID().IsValid() || player->GetTrackRidingID() != InvalidTrackID)
				{
					ResetPlayerTracking();
					return;
				}

				const glm::vec3 velocity = ToVec3(player->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity());
				const Traversal* traversal = player->GetTraversal();
				const Traversal::Mode mode = traversal != nullptr ? traversal->GetMode() : Traversal::Mode::NONE;
				const bool bGrounded = player->IsGrounded();

				// Landing: after enough time in the air & falling fast enough
				if (!bGrounded)
				{
					s_AirTime += dt;
					s_MinVelY = glm::min(s_MinVelY, velocity.y);
				}
				else
				{
					if (!s_bWasGrounded && s_AirTime > 0.12f && s_MinVelY < -2.0f)
					{
						PlayLand(glm::clamp((-s_MinVelY - 2.0f) / 16.0f, 0.0f, 1.0f));
						s_StepPhase = 0.0f;
					}
					s_AirTime = 0.0f;
					s_MinVelY = 0.0f;
				}
				s_bWasGrounded = bGrounded;
				s_PlayerAirSpeed = (bGrounded && mode == Traversal::Mode::NONE) ? 0.0f : glm::length(velocity);

				// Footsteps: cadence rises with speed
				const real halfHeight = player->GetHeight() * 0.5f;
				const glm::vec3 feetPos = player->GetTransform()->GetWorldPosition() - glm::vec3(0.0f, halfHeight, 0.0f);
				const real horizontalSpeed = glm::length(glm::vec2(velocity.x, velocity.z));
				if (mode == Traversal::Mode::NONE && bGrounded && horizontalSpeed > 0.8f)
				{
					s_StepPhase += dt * glm::clamp(1.4f + 0.22f * horizontalSpeed, 1.4f, 3.6f);
					if (s_StepPhase >= 1.0f)
					{
						s_StepPhase -= 1.0f;
						PlayFootstep(GetSurfaceAt(feetPos, player->GetGroundNormal()), 0.4f + 0.4f * glm::clamp(horizontalSpeed / 9.0f, 0.0f, 1.0f));
					}
				}
				else if (mode == Traversal::Mode::CLIMBING && glm::length(velocity) > 0.3f)
				{
					// Hands & feet scuffing on the wall
					s_StepPhase += dt * 1.8f;
					if (s_StepPhase >= 1.0f)
					{
						s_StepPhase -= 1.0f;
						PlayFootstep(Surface::ROCK, 0.3f);
					}
				}
				else
				{
					// The first step comes soon after starting to move
					s_StepPhase = glm::max(s_StepPhase, 0.7f);
				}
			}

			void UpdateAmbience(real dt, bool bPaused)
			{
				// Terrain queries are only refreshed a few times per second
				s_AmbienceTimer -= dt;
				if (s_AmbienceTimer <= 0.0f)
				{
					s_AmbienceTimer = 0.25f;
					TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
					s_bHasTerrain = terrainGenerator != nullptr;
					if (s_bHasTerrain)
					{
						const real surfaceHeight = terrainGenerator->SampleHeight(glm::vec2(s_ListenerPos.x, s_ListenerPos.z));
						s_HeightAboveGround = s_ListenerPos.y - surfaceHeight;
						// Well below the heightfield surface: in a cave
						s_Enclosed = synth::SmoothStep(2.0f, 10.0f, -s_HeightAboveGround);
						s_Ocean = glm::clamp(terrainGenerator->SampleColumn(s_ListenerPos.x, s_ListenerPos.z).oceanWeight, 0.0f, 1.0f);
					}
				}

				// Gusts: a slowly wandering target
				s_GustTimer -= dt;
				if (s_GustTimer <= 0.0f)
				{
					s_GustTimer = s_Rng.Range(1.5f, 5.0f);
					s_GustTarget = s_Rng.Range(0.15f, 1.0f);
				}
				s_Gust += (s_GustTarget - s_Gust) * glm::min(dt * 0.6f, 1.0f);

				if (!s_bHasTerrain || bPaused)
				{
					s_Wind.target = 0.0f;
					s_Crickets.target = 0.0f;
					s_AirRush.target = 0.0f;
					return;
				}

				const real altitude = synth::SmoothStep(4.0f, 150.0f, s_HeightAboveGround);
				const real open = 1.0f - s_Enclosed * 0.9f;
				s_Wind.target = s_AmbienceVolume * (0.1f + 0.3f * altitude + 0.12f * s_Ocean) * (0.55f + 0.45f * s_Gust) * open;
				s_Wind.pitch = 0.85f + 0.25f * s_Gust + 0.1f * altitude;

				// Rushing air: audible from a fast fall, roaring in a steep wingsuit dive
				const real rush = synth::SmoothStep(6.0f, 55.0f, s_PlayerAirSpeed);
				s_AirRush.target = s_SfxVolume * glm::min(1.6f * rush, 1.0f);
				s_AirRush.pitch = 0.75f + 0.75f * rush;

				// Crickets at night, near the ground, away from the sea & out of caves
				BaseScene* scene = g_SceneManager->CurrentScene();
				const real sunHeight = scene != nullptr ? BaseScene::GetSunDirection(scene->GetTimeOfDay()).y : 1.0f;
				const real night = 1.0f - synth::SmoothStep(-0.12f, 0.08f, sunHeight);
				const real nearGround = 1.0f - synth::SmoothStep(20.0f, 80.0f, s_HeightAboveGround);
				s_Crickets.target = s_AmbienceVolume * 0.3f * night * nearGround * (1.0f - s_Enclosed) * (1.0f - s_Ocean);
				s_Crickets.pitch = 1.0f;
			}

			void UpdateReportedLoops(real dt, bool bPaused)
			{
				const real slipTarget = s_bVehicleReported ? s_VehicleSlip : 0.0f;
				s_VehicleSlipSmoothed += (slipTarget - s_VehicleSlipSmoothed) * glm::min(dt * (slipTarget > s_VehicleSlipSmoothed ? 3.0f : 6.0f), 1.0f);

				if (s_bVehicleReported && !bPaused)
				{
					const real speed = std::abs(s_VehicleSpeed);
					const real speedFactor = glm::clamp(speed / 35.0f, 0.0f, 1.0f);
					const real throttle = glm::clamp(std::abs(s_VehicleThrottle), 0.0f, 1.0f);
					s_Engine.target = s_SfxVolume * (0.3f + 0.25f * throttle + 0.25f * speedFactor);
					s_Engine.pitch = glm::clamp(0.6f + 1.2f * speedFactor + 0.2f * throttle, 0.5f, 2.0f);

					const real brakeSkid = s_bVehicleBraking ? synth::SmoothStep(1.0f, 8.0f, speed) : 0.0f;
					const real slideSkid = 0.6f * synth::SmoothStep(0.5f, 0.9f, s_VehicleSlipSmoothed) * synth::SmoothStep(2.0f, 10.0f, speed);
					s_Skid.target = s_SfxVolume * 0.45f * glm::max(brakeSkid, slideSkid);
					s_Skid.pitch = 0.9f + 0.2f * speedFactor;
				}
				else
				{
					s_Engine.target = 0.0f;
					s_Skid.target = 0.0f;
				}

				if (s_bFireReported && !bPaused)
				{
					AudioManager::SetSourcePositionWS(s_Banks[(u32)Sound::LOOP_CAMPFIRE].ids[0], s_FirePos);
					s_Fire.target = s_SfxVolume * 0.9f * glm::clamp(s_FireIntensity, 0.0f, 1.0f);
				}
				else
				{
					s_Fire.target = 0.0f;
				}

				if (s_bTreasureReported && !bPaused)
				{
					AudioManager::SetSourcePositionWS(s_Banks[(u32)Sound::LOOP_TREASURE_HUM].ids[0], s_TreasurePos);
					s_TreasureHum.target = s_SfxVolume * 0.35f;
				}
				else
				{
					s_TreasureHum.target = 0.0f;
				}
			}
		} // namespace

		void Initialize()
		{
			if (s_bGenerating || s_bReady)
			{
				return;
			}

			// FLEX_DUMP_SOUNDS=<any value>: writes every generated sound to saved/sound_dump/ as a .wav & prints each one's length & level
			s_bDump = getenv("FLEX_DUMP_SOUNDS") != nullptr;
			s_bLog = getenv("FLEX_SOUND_LOG") != nullptr;
			if (s_bDump)
			{
				s_DumpDirectory = RelativePathToAbsolute(SAVED_DIRECTORY "sound_dump/");
				Platform::CreateDirectoryRecursive(s_DumpDirectory);
			}

			s_Generated.clear();
			s_Generated.resize((u32)Sound::_COUNT);
			for (u32 s = 0; s < (u32)Sound::_COUNT; ++s)
			{
				s_Generated[s].resize(sound_recipes::GetSoundDesc((Sound)s).variantCount);
			}

			s_GenerationStart = Clock::now();
			s_GenerationEndMicros = 0;
			s_GenerationCPUMicros = 0;
			s_bGenerating = true;

			// Loops are the longest to generate, so they're queued first
			for (i32 pass = 0; pass < 2; ++pass)
			{
				for (u32 s = 0; s < (u32)Sound::_COUNT; ++s)
				{
					const sound_recipes::SoundDesc& desc = sound_recipes::GetSoundDesc((Sound)s);
					if (desc.bLoop != (pass == 0))
					{
						continue;
					}
					for (u32 v = 0; v < desc.variantCount; ++v)
					{
						JobSystem::Execute(s_Context, [](JobSystem::JobArgs args)
						{
							GenerateJob((u32)(args.payload >> 16), (u32)(args.payload & 0xFFFF));
						}, ((u64)s << 16) | (u64)v);
					}
				}
			}
		}

		void Destroy()
		{
			if (s_bGenerating)
			{
				JobSystem::Wait(s_Context);
				s_bGenerating = false;
			}
			s_Generated.clear();
			// Sources themselves are destroyed with all others in AudioManager::Destroy
			for (SoundBank& bank : s_Banks)
			{
				bank.ids.clear();
			}
			for (Loop* loop : s_Loops)
			{
				loop->gain = 0.0f;
				loop->bPlaying = false;
			}
			s_bReady = false;
		}

		void Update()
		{
			if (s_bGenerating && !JobSystem::IsBusy(s_Context))
			{
				Upload();
			}

			if (s_bReady)
			{
				PROFILE_AUTO("GameSounds Update");

				const real dt = glm::min(g_UnpausedDeltaTime, 0.1f);
				const bool bPaused = g_EngineInstance->IsSimulationPaused();

				UpdateListener();
				UpdatePlayer(dt, bPaused);
				UpdateAmbience(dt, bPaused);
				UpdateReportedLoops(dt, bPaused);
				for (Loop* loop : s_Loops)
				{
					UpdateLoop(*loop, dt);
				}
			}

			s_bVehicleReported = false;
			s_bFireReported = false;
			s_bTreasureReported = false;
		}

		bool IsReady()
		{
			return s_bReady;
		}

		void SetSfxVolume(real volume)
		{
			s_SfxVolume = glm::clamp(volume, 0.0f, 1.0f);
		}

		void SetAmbienceVolume(real volume)
		{
			s_AmbienceVolume = glm::clamp(volume, 0.0f, 1.0f);
		}

		void PlayFootstep(Surface surface, real gain /* = 1.0f */)
		{
			static const real surfaceGains[] = { 0.7f, 0.75f, 0.65f, 0.7f, 0.8f, 0.75f };
			static_assert(ARRAY_LENGTH(surfaceGains) == (u32)Surface::_COUNT, "surfaceGains must match Surface");
			const u32 index = glm::min((u32)surface, (u32)Surface::_COUNT - 1);
			PlayOneShot((Sound)((u32)Sound::FOOTSTEP_GRASS + index), gain * surfaceGains[index], 0.06f);
		}

		void PlayJump()
		{
			PlayOneShot(Sound::JUMP, 0.55f, 0.05f);
		}

		void PlayLand(real intensity)
		{
			intensity = glm::clamp(intensity, 0.0f, 1.0f);
			PlayOneShot(intensity > 0.5f ? Sound::LAND_HEAVY : Sound::LAND_LIGHT, 0.45f + 0.45f * intensity, 0.05f);
		}

		void PlayGliderStow()
		{
			PlayOneShot(Sound::GLIDER_STOW, 0.6f, 0.04f);
		}

		void PlayPlace(const glm::vec3& posWS)
		{
			FLEX_UNUSED(posWS);
			// Always near the player, so not positional
			PlayOneShot(Sound::PLACE, 0.7f, 0.08f);
		}

		void PlayTorchIgnite(const glm::vec3& posWS)
		{
			PlayOneShotAt(Sound::TORCH_IGNITE, posWS, 0.8f, 0.08f);
		}

		void PlayCampfireIgnite(const glm::vec3& posWS)
		{
			PlayOneShotAt(Sound::CAMPFIRE_IGNITE, posWS, 0.9f, 0.03f);
		}

		void PlayChop(const glm::vec3& posWS)
		{
			PlayOneShotAt(Sound::CHOP, posWS, 0.8f, 0.08f);
		}

		void PlayTreeFall(const glm::vec3& posWS)
		{
			PlayOneShotAt(Sound::TREE_FALL, posWS, 1.0f, 0.05f);
		}

		void PlayUITick()
		{
			PlayOneShot(Sound::UI_TICK, 0.5f, 0.02f);
		}

		void PlayUIConfirm()
		{
			PlayOneShot(Sound::UI_CONFIRM, 0.45f, 0.0f);
		}

		void ReportFire(const glm::vec3& posWS, real intensity)
		{
			const glm::vec3 diff = posWS - s_ListenerPos;
			const real distSq = glm::dot(diff, diff);
			if (!s_bFireReported || distSq < s_FireDistSq)
			{
				s_bFireReported = true;
				s_FirePos = posWS;
				s_FireIntensity = intensity;
				s_FireDistSq = distSq;
			}
		}

		void ReportTreasure(const glm::vec3& posWS)
		{
			const glm::vec3 diff = posWS - s_ListenerPos;
			const real distSq = glm::dot(diff, diff);
			if (!s_bTreasureReported || distSq < s_TreasureDistSq)
			{
				s_bTreasureReported = true;
				s_TreasurePos = posWS;
				s_TreasureDistSq = distSq;
			}
		}

		void UpdateVehicle(real forwardSpeed, real throttle, bool bBraking, real wheelSlip)
		{
			s_bVehicleReported = true;
			s_VehicleSpeed = forwardSpeed;
			s_VehicleThrottle = throttle;
			s_bVehicleBraking = bBraking;
			s_VehicleSlip = wheelSlip;
		}

		Surface GetSurfaceAt(const glm::vec3& footPosWS, const glm::vec3& groundNormal)
		{
			TerrainGenerator* terrainGenerator = TerrainGenerator::GetActive();
			if (terrainGenerator == nullptr)
			{
				return Surface::ROCK;
			}

			const terrain::ColumnSample column = terrainGenerator->SampleColumn(footPosWS.x, footPosWS.z);
			if (column.lakeID != 0 && footPosWS.y < column.waterLevel + 0.15f)
			{
				return Surface::WATER;
			}

			// Standing on something well above the terrain (built floors etc.)
			if (terrainGenerator->SampleDensity(footPosWS - groundNormal * 0.1f) > 0.5f)
			{
				return Surface::WOOD;
			}

			// Weights: x: grass, y: rock, z: granular (sand & gravel), w: snow
			const glm::vec4 weights = terrainGenerator->SampleSurface(footPosWS, groundNormal).weights;
			if (weights.w >= weights.x && weights.w >= weights.y && weights.w >= weights.z)
			{
				return Surface::SNOW;
			}
			if (weights.x >= weights.y && weights.x >= weights.z)
			{
				return Surface::GRASS;
			}
			return weights.z >= weights.y ? Surface::GRAVEL : Surface::ROCK;
		}
	} // namespace GameSounds
} // namespace flex
