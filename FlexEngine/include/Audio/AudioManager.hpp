#pragma once

#include "AL/al.h"
#include "AL/alc.h"

#include <string>
#include <vector>

namespace flex
{
	class AudioManager
	{
	public:
		static void Initialize();
		static void Destroy();

		static AudioSourceID AddAudioSource(const std::string& filePath);

		static void PlayAudioSource(AudioSourceID sourceID);
		static void PauseAudioSource(AudioSourceID sourceID);
		static void StopAudioSource(AudioSourceID sourceID);

		/* Volume of sound [0.0, 1.0] (logarithmic) */
		static void SetAudioSourceGain(AudioSourceID sourceID, real gain);
		static real GetAudioSourceGain(AudioSourceID sourceID);

		/* Pitch of sound [0.5, 2.0] */
		static void SetAudioSourcePitch(AudioSourceID sourceID, real pitch);
		static real GetAudioSourcePitch(AudioSourceID sourceID);

		static void SetAudioSourceLooping(AudioSourceID sourceID, bool looping);
		static bool GetAudioSourceLooping(AudioSourceID sourceID);

	private:
		static void DisplayALError(const std::string& str, ALenum error);

		static void PrintAudioDevices(const ALCchar* devices);

		static const i32 NUM_BUFFERS = 32;
		static ALuint m_Buffers[NUM_BUFFERS];

		struct AudioSource
		{
			ALuint source;
			real gain = 1.0f;
			real pitch = 1.0f;

			// AL_INITIAL, AL_PLAYING, AL_PAUSED, or AL_STOPPED
			ALint state = AL_INITIAL;

			bool bLooping = false;
		};

		static std::vector<AudioSource> m_Sources;

		static ALCdevice* s_Device;
		static ALCcontext* s_Context;

		AudioManager() = delete;
		~AudioManager() = delete;
	};
} // namespace flex
