#pragma once

IGNORE_WARNINGS_PUSH
#include "AL/al.h"
#include "AL/alc.h"
IGNORE_WARNINGS_POP

namespace flex
{
	class AudioManager
	{
	public:
		static void Initialize();
		static void Destroy();

		static AudioSourceID AddAudioSource(const std::string& filePath);
		static AudioSourceID SynthesizeSound(sec length, real freq);
		static bool DestroyAudioSource(AudioSourceID sourceID);
		static void ClearAllAudioSources();

		/* [0.0, 1.0] logarithmic */
		static void SetMasterGain(real masterGain);
		static real GetMasterGain();

		static void PlaySource(AudioSourceID sourceID, bool bForceRestart = true);
		static void PauseSource(AudioSourceID sourceID);
		static void StopSource(AudioSourceID sourceID);

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

		static void DrawImGuiObjects();

	private:

		static void DisplayALError(const std::string& str, ALenum error);

		static void PrintAudioDevices(const ALCchar* devices);

		static ALuint GetNextAvailableSourceAndBufferIndex();

		static const i32 NUM_BUFFERS = 32;
		static ALuint s_Buffers[NUM_BUFFERS];

		struct Source
		{
			ALuint source = InvalidAudioSourceID;
			real gain = 1.0f;
			real pitch = 1.0f;

			// AL_INITIAL, AL_PLAYING, AL_PAUSED, or AL_STOPPED
			ALenum state = AL_INITIAL;

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
