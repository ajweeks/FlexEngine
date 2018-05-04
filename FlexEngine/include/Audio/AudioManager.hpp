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

		static void PlaySource(AudioSourceID sourceID);
		static void PauseSource(AudioSourceID sourceID);
		static void StopSource(AudioSourceID sourceID);

		/* Volume of sound [0.0, 1.0] (logarithmic) */
		static void SetSourceGain(AudioSourceID sourceID, real gain);
		static real GetSourceGain(AudioSourceID sourceID);

		/* Pitch of sound [0.5, 2.0] */
		static void SetSourcePitch(AudioSourceID sourceID, real pitch);
		static real GetSourcePitch(AudioSourceID sourceID);

		static void SetSourceLooping(AudioSourceID sourceID, bool looping);
		static bool GetSourceLooping(AudioSourceID sourceID);

	private:
		static void DisplayALError(const std::string& str, ALenum error);

		static void PrintAudioDevices(const ALCchar* devices);

		static const i32 NUM_BUFFERS = 32;
		static ALuint s_Buffers[NUM_BUFFERS];

		struct Source
		{
			ALuint source;
			real gain = 1.0f;
			real pitch = 1.0f;

			// AL_INITIAL, AL_PLAYING, AL_PAUSED, or AL_STOPPED
			ALint state = AL_INITIAL;

			bool bLooping = false;
		};

		static std::vector<Source> s_Sources;

		static ALCdevice* s_Device;
		static ALCcontext* s_Context;

		AudioManager() = delete;
		~AudioManager() = delete;
	};
} // namespace flex
