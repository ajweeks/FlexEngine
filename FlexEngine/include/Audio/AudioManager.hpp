#pragma once

IGNORE_WARNINGS_PUSH
#include "AL/al.h"
#include "AL/alc.h"
IGNORE_WARNINGS_POP

#include "Histogram.hpp"

namespace flex
{
	struct SoundClip_Looping
	{
		SoundClip_Looping();
		SoundClip_Looping(const char* name, AudioSourceID startID, AudioSourceID loopID, AudioSourceID endID);

		bool IsValid() const;
		bool IsPlaying() const;

		void Start();
		void End();
		void Update();
		void KillCurrentlyPlaying();

		void DrawImGui();

		enum class State
		{
			STARTING,
			LOOPING,
			ENDING,

			OFF
		};

		static constexpr const char* StateStrings[] =
		{
			"Starting",
			"Looping",
			"Ending",
			"Off"
		};

		static_assert(ARRAY_LENGTH(StateStrings) == (u32)State::OFF + 1, "Length of StateStrings must match length of State enum");

		// Optional
		AudioSourceID start = InvalidAudioSourceID;

		AudioSourceID loop = InvalidAudioSourceID;

		// Optional
		AudioSourceID end = InvalidAudioSourceID;

		State state = State::OFF;
		real timeInState = 0.0f;
		real stateLength = -1.0f;

	private:
		void ChangeToState(State newState);

		real m_FadeFastDuration = 0.1f;
		real m_FadeDuration = 0.3f;

		// Debug only
		const char* m_Name = nullptr;
		Histogram m_TimeInStateHisto;

	};

	struct SoundClip_LoopingSimple
	{
		SoundClip_LoopingSimple();
		SoundClip_LoopingSimple(const char* name, AudioSourceID loopID);

		bool IsValid() const;
		bool IsPlaying() const;

		void FadeIn();
		void FadeOut();
		void Update();
		void KillCurrentlyPlaying();

		void SetPitch(real pitch);

		void DrawImGui();

		AudioSourceID loop = InvalidAudioSourceID;

		real fadeInTimeRemaining = -1.0f;
		real fadeOutTimeRemaining = -1.0f;

	private:
		real m_FadeFastDuration = 0.1f;
		real m_FadeDuration = 0.3f;
		bool bPlaying = false;

		// Debug only
		const char* m_Name = nullptr;
		Histogram m_VolHisto;

	};


	// TODO: Make global instance like every other manager?
	// TODO: Inherit from System base class?
	class AudioManager
	{
	public:
		static void Initialize();
		static void Destroy();
		static void Update();

		static AudioSourceID AddAudioSource(const std::string& filePath);
		static AudioSourceID SynthesizeSound(sec length, real freq);
		static bool DestroyAudioSource(AudioSourceID sourceID);
		static void ClearAllAudioSources();

		/* [0.0, 1.0] logarithmic */
		static void SetMasterGain(real masterGain);
		static real GetMasterGain();

		static void PlaySource(AudioSourceID sourceID, bool bForceRestart = true);
		// Start source partway through (t in [0, 1])
		static void PlaySourceFromPos(AudioSourceID sourceID, real t);
		static void PauseSource(AudioSourceID sourceID);
		static void StopSource(AudioSourceID sourceID);

		static void FadeSourceIn(AudioSourceID sourceID, real fadeDuration, real fadeMaxDuration);
		static void FadeSourceOut(AudioSourceID sourceID, real fadeDuration, real fadeMaxDuration);

		/*
		* Multiplies the source by gainScale
		* Optionally prevents gain from reaching zero so that it
		* can be scale up again later
		*/
		static void ScaleSourceGain(AudioSourceID sourceID, real gainScale, bool bPreventZero = true);

		/* Volume of sound [0.0, 1.0] (logarithmic) */
		static void SetSourceGain(AudioSourceID sourceID, real gain);
		static real GetSourceGain(AudioSourceID sourceID);

		static void AddToSourcePitch(AudioSourceID sourceID, real deltaPitch);

		/* Pitch of sound [0.5, 2.0] */
		static void SetSourcePitch(AudioSourceID sourceID, real pitch);
		static real GetSourcePitch(AudioSourceID sourceID);

		static void SetSourceLooping(AudioSourceID sourceID, bool bLooping);
		static bool GetSourceLooping(AudioSourceID sourceID);

		static bool IsSourcePlaying(AudioSourceID sourceID);

		static real GetSoundLength(AudioSourceID sourceID);

		static void ToggleMuted();
		static void SetMuted(bool bMuted);
		static bool IsMuted();

		static void DrawImGuiObjects();

	private:

		static void DisplayALError(const std::string& str, ALenum error);

		static void PrintAudioDevices(const ALCchar* devices);

		static ALuint GetNextAvailableSourceAndBufferIndex();

		static const i32 NUM_BUFFERS = 32;
		static ALuint s_Buffers[NUM_BUFFERS];

		static real s_MasterGain;
		static bool s_Muted;

		struct Source
		{
			ALuint source = InvalidAudioSourceID;
			real gain = 1.0f;
			real pitch = 1.0f;
			real length = -1.0f;

			// AL_INITIAL, AL_PLAYING, AL_PAUSED, or AL_STOPPED
			ALenum state = AL_INITIAL;

			// If non-zero, we're fading in when bFadingIn, and out otherwise
			real fadeDuration = 0.0f;
			real fadeDurationRemaining = 0.0f;
			bool bFadingIn;

			bool bLooping = false;
		};

		static std::array<Source, NUM_BUFFERS> s_Sources;

		static ALCdevice* s_Device;
		static ALCcontext* s_Context;

		static AudioSourceID s_BeepID;

		AudioManager() = delete;
		~AudioManager() = delete;
	};
} // namespace flex
