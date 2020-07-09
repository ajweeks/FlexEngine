#include "stdafx.hpp"

#include "Audio/AudioManager.hpp"

#include <imgui/imgui_internal.h> // For PushItemFlag

#include "Helpers.hpp"

namespace flex
{
	std::array<AudioManager::Source, AudioManager::NUM_BUFFERS> AudioManager::s_Sources;

	ALCcontext* AudioManager::s_Context = nullptr;
	ALCdevice* AudioManager::s_Device = nullptr;

	ALuint AudioManager::s_Buffers[NUM_BUFFERS];

	real AudioManager::s_MasterGain = 0.2f;
	bool AudioManager::s_Muted = false;

	AudioSourceID AudioManager::s_BeepID = InvalidAudioSourceID;

	void AudioManager::Initialize()
	{
		// Retrieve preferred device
		s_Device = alcOpenDevice(NULL);

		if (!s_Device)
		{
			PrintError("Failed to create an openAL device!\n");
			return;
		}

		{
			i32 maj;
			alcGetIntegerv(s_Device, ALC_MAJOR_VERSION, sizeof(maj), &maj);
			i32 min;
			alcGetIntegerv(s_Device, ALC_MINOR_VERSION, sizeof(min), &min);

			Print("OpenAL v%d.%d\n", maj, min);
		}

		constexpr bool printAvailableAudioDeviceNames = false;
		if (printAvailableAudioDeviceNames)
		{
			PrintAudioDevices(alcGetString(NULL, ALC_DEVICE_SPECIFIER));
		}

		const ALchar* deviceName = alcGetString(s_Device, ALC_DEVICE_SPECIFIER);
		Print("Chosen audio device: %s\n", deviceName);

		s_Context = alcCreateContext(s_Device, NULL);
		alcMakeContextCurrent(s_Context);

		//bool eaxPresent = (alIsExtensionPresent("EAX2.0") == GL_TRUE);

		alGenBuffers(NUM_BUFFERS, s_Buffers);
		ALenum error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alGenBuffers", error);
			return;
		}

		// Reserve first ID for beep to play on volume change
		s_BeepID = AudioManager::AddAudioSource(SFX_LOCATION "wah-wah-02.wav");

		SetMasterGain(s_MasterGain);
	}

	void AudioManager::Destroy()
	{
		ClearAllAudioSources();
		alcMakeContextCurrent(NULL);
		alcDestroyContext(s_Context);
		alcCloseDevice(s_Device);
	}

	AudioSourceID AudioManager::AddAudioSource(const std::string& filePath)
	{
		AudioSourceID newID = GetNextAvailableSourceAndBufferIndex();
		if (newID == InvalidAudioSourceID)
		{
			// TODO: Resize buffers dynamically
			PrintError("Failed to add new audio source! All %d buffers are in use\n", NUM_BUFFERS);
			return InvalidAudioSourceID;
		}

		if (g_bEnableLogging_Loading)
		{
			const std::string friendlyName = StripLeadingDirectories(filePath);
			Print("Loading audio source %s\n", friendlyName.c_str());
		}

		// WAVE file
		i32 format;
		u8* data;
		i32 size;
		i32 freq;
		if (!ParseWAVFile(filePath, &format, &data, &size, &freq))
		{
			PrintError("Failed to open or parse WAVE file\n");
			return InvalidAudioSourceID;
		}

		// Buffer
		alBufferData(s_Buffers[newID], format, data, size, freq);
		ALenum error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alBufferData", error);
			alDeleteBuffers(NUM_BUFFERS, s_Buffers);
			return InvalidAudioSourceID;
		}
		delete[] data;

		// Source
		alGenSources(1, &s_Sources[newID].source);
		error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alGenSources 1", error);
			return InvalidAudioSourceID;
		}

		alSourcei(s_Sources[newID].source, AL_BUFFER, s_Buffers[newID]);
		DisplayALError("alSourcei", alGetError());

		return newID;
	}

	AudioSourceID AudioManager::SynthesizeSound(sec length, real freq)
	{
		AudioSourceID newID = GetNextAvailableSourceAndBufferIndex();
		if (newID == InvalidAudioSourceID)
		{
			PrintError("Failed to add new audio source! All %d buffers are in use\n", NUM_BUFFERS);
			return InvalidAudioSourceID;
		}

		// WAVE file
		i32 format = AL_FORMAT_STEREO8;
		i32 sampleRate = 44100;
		i32 size = (i32)(sampleRate * length);
		u8* data = (u8*)malloc((u32)size);

		// See http://iquilezles.org/apps/soundtoy/index.html for more patterns
		for (i32 i = 0; i < size; ++i)
		{
			real t = (real)i / (real)(size - 1);
			//t -= fmod(t, 0.5f); // Linear fade in/out
			//t = pow(sin(t* PI), 0.01f); // Sinusodal fade in/out
			real y = 6.0f * t * exp(-2.0f * t) * sin(freq * t);
			y *= 0.8f + 0.2f * cos(16.0f * t);
			data[i] = (u8)(y * 15.0f);
		}

		ALenum error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("OpenAndParseWAVFile", error);
			alDeleteBuffers(NUM_BUFFERS, s_Buffers);
			return InvalidAudioSourceID;
		}


		// Buffer
		alBufferData(s_Buffers[newID], format, data, size, sampleRate);
		error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alBufferData", error);
			alDeleteBuffers(NUM_BUFFERS, s_Buffers);
			return InvalidAudioSourceID;
		}
		free(data);


		// Source
		alGenSources(1, &s_Sources[newID].source);
		error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alGenSources 1", error);
			return InvalidAudioSourceID;
		}

		alSourcei(s_Sources[newID].source, AL_BUFFER, s_Buffers[newID]);
		DisplayALError("alSourcei", alGetError());

		return newID;
	}

	bool AudioManager::DestroyAudioSource(AudioSourceID sourceID)
	{
		if (sourceID >= NUM_BUFFERS || s_Sources[sourceID].source == InvalidAudioSourceID)
		{
			return false;
		}

		alDeleteSources(1, &s_Sources[sourceID].source);
		s_Sources[sourceID].source = InvalidAudioSourceID;
		return true;
	}

	void AudioManager::ClearAllAudioSources()
	{
		for (Source& source : s_Sources)
		{
			if (source.source != InvalidAudioSourceID)
			{
				alDeleteSources(1, &source.source);
			}
		}
		s_Sources.fill({});
	}

	void AudioManager::SetMasterGain(real masterGain)
	{
		s_MasterGain = masterGain;
		alListenerf(AL_GAIN, masterGain);
	}

	real AudioManager::GetMasterGain()
	{
		return s_MasterGain;
	}

	void AudioManager::PlaySource(AudioSourceID sourceID, bool bForceRestart)
	{
		if (sourceID >= s_Sources.size())
		{
			PrintError("Attempted to play invalid source %d\n", (u32)sourceID);
			return;
		}

		alGetSourcei(s_Sources[sourceID].source, AL_SOURCE_STATE, &s_Sources[sourceID].state);

		if (bForceRestart || s_Sources[sourceID].state != AL_PLAYING)
		{
			alSourcePlay(s_Sources[sourceID].source);
			alGetSourcei(s_Sources[sourceID].source, AL_SOURCE_STATE, &s_Sources[sourceID].state);
			DisplayALError("PlaySource", alGetError());
		}
	}

	void AudioManager::PauseSource(AudioSourceID sourceID)
	{
		if (sourceID >= s_Sources.size())
		{
			PrintError("Attempted to pause invalid source %d\n", (u32)sourceID);
			return;
		}

		alGetSourcei(s_Sources[sourceID].source, AL_SOURCE_STATE, &s_Sources[sourceID].state);

		if (s_Sources[sourceID].state != AL_PAUSED)
		{
			alSourcePause(s_Sources[sourceID].source);
			alGetSourcei(s_Sources[sourceID].source, AL_SOURCE_STATE, &s_Sources[sourceID].state);
			DisplayALError("PauseSource", alGetError());
		}
	}

	void AudioManager::StopSource(AudioSourceID sourceID)
	{
		if (sourceID >= s_Sources.size())
		{
			PrintError("Attempted to stop invalid source %d\n", (u32)sourceID);
			return;
		}

		alGetSourcei(s_Sources[sourceID].source, AL_SOURCE_STATE, &s_Sources[sourceID].state);

		if (s_Sources[sourceID].state != AL_STOPPED)
		{
			alSourceStop(s_Sources[sourceID].source);
			alGetSourcei(s_Sources[sourceID].source, AL_SOURCE_STATE, &s_Sources[sourceID].state);
			DisplayALError("StopSource", alGetError());
		}
	}

	void AudioManager::ScaleSourceGain(AudioSourceID sourceID, real gainScale, bool bPreventZero)
	{
		assert(sourceID < s_Sources.size());

		const real epsilon = 0.00001f;

		if (bPreventZero && s_Sources[sourceID].gain == 0.0f)
		{
			s_Sources[sourceID].gain = epsilon;
		}

		real newGain = s_Sources[sourceID].gain * gainScale;
		if (bPreventZero && newGain < epsilon)
		{
			// Prevent gain from reaching 0, so it can be scaled up again
			newGain = epsilon;
		}
		SetSourceGain(sourceID, newGain);
	}

	void AudioManager::SetSourceGain(AudioSourceID sourceID, real gain)
	{
		assert(sourceID < s_Sources.size());

		gain = Saturate(gain);

		if (s_Sources[sourceID].gain != gain)
		{
			s_Sources[sourceID].gain = gain;
			alSourcef(s_Sources[sourceID].source, AL_GAIN, gain);

			DisplayALError("SetSourceGain", alGetError());
		}
	}

	real AudioManager::GetSourceGain(AudioSourceID sourceID)
	{
		assert(sourceID < s_Sources.size());

		return s_Sources[sourceID].gain;
	}

	void AudioManager::AddToSourcePitch(AudioSourceID sourceID, real deltaPitch)
	{
		assert(sourceID < s_Sources.size());

		SetSourcePitch(sourceID, s_Sources[sourceID].pitch + deltaPitch);
	}

	void AudioManager::SetSourceLooping(AudioSourceID sourceID, bool bLooping)
	{
		assert(sourceID < s_Sources.size());

		if (s_Sources[sourceID].bLooping != bLooping)
		{
			s_Sources[sourceID].bLooping = bLooping;
			alSourcei(s_Sources[sourceID].source, AL_LOOPING, bLooping ? AL_TRUE : AL_FALSE);

			DisplayALError("SetSourceLooping", alGetError());
		}
	}

	bool AudioManager::GetSourceLooping(AudioSourceID sourceID)
	{
		assert(sourceID < s_Sources.size());

		return s_Sources[sourceID].bLooping;
	}

	bool AudioManager::IsSourcePlaying(AudioSourceID sourceID)
	{
		assert(sourceID < s_Sources.size());

		alGetSourcei(s_Sources[sourceID].source, AL_SOURCE_STATE, &s_Sources[sourceID].state);
		return (s_Sources[sourceID].state == AL_PLAYING);
	}

	void AudioManager::ToggleMuted()
	{
		SetMuted(!s_Muted);
	}

	void AudioManager::SetMuted(bool bMuted)
	{
		if (s_Muted != bMuted)
		{
			s_Muted = bMuted;

			if (s_Muted)
			{
				alListenerf(AL_GAIN, 0.0f);
			}
			else
			{
				alListenerf(AL_GAIN, s_MasterGain);
			}
		}
	}

	bool AudioManager::IsMuted()
	{
		return s_Muted;
	}

	void AudioManager::DrawImGuiObjects()
	{
		if (ImGui::TreeNode("Audio"))
		{
			bool bMuted = s_Muted;
			if (ImGui::Checkbox("Muted", &bMuted))
			{
				ToggleMuted();
			}

			const bool bWasMuted = s_Muted;
			if (bWasMuted)
			{
				ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
			}

			real gain = s_MasterGain;
			if (ImGui::SliderFloat("Master volume", &gain, 0.0f, 1.0f))
			{
				SetMasterGain(gain);
			}

			if (bWasMuted)
			{
				ImGui::PopItemFlag();
				ImGui::PopStyleVar();
			}

			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				PlaySource(s_BeepID, true);
			}

			ImGui::TreePop();
		}
	}

	void AudioManager::SetSourcePitch(AudioSourceID sourceID, real pitch)
	{
		assert(sourceID < s_Sources.size());

		// openAL range (found in al.h)
		pitch = glm::clamp(pitch, 0.5f, 2.0f);

		if (s_Sources[sourceID].pitch != pitch)
		{
			s_Sources[sourceID].pitch = pitch;
			alSourcef(s_Sources[sourceID].source, AL_PITCH, pitch);

			DisplayALError("SetSourcePitch", alGetError());
		}
	}

	real AudioManager::GetSourcePitch(AudioSourceID sourceID)
	{
		assert(sourceID < s_Sources.size());

		return s_Sources[sourceID].pitch;
	}

	void AudioManager::DisplayALError(const std::string& str, ALenum error)
	{
		const char* cStr = str.c_str();
		switch (error)
		{
		case AL_NO_ERROR:			return;
		case AL_INVALID_NAME:		PrintError("ALError: Invalid name - %s\n", cStr); break;
		case AL_ILLEGAL_ENUM:		PrintError("ALError: Invalid enum - %s\n", cStr); break;
		case AL_INVALID_VALUE:		PrintError("ALError: Invalid value - %s\n", cStr); break;
		case AL_INVALID_OPERATION:	PrintError("ALError: Invalid operation - %s\n", cStr); break;
		case AL_OUT_OF_MEMORY:		PrintError("ALError: Out of memory - %s\n", cStr); break;
		default:					PrintError("ALError: Unknown error - %s\n", cStr); break;
		}
	}

	void AudioManager::PrintAudioDevices(const ALCchar* devices)
	{
		const ALCchar* device = devices;
		const ALCchar* next = devices + 1;

		Print("Available OpenAL devices:\n");
		while (device && *device != '\0' &&
			next && *next != '\0')
		{
			Print("\t\t%s\n", device);
			size_t len = strlen(device);
			device += (len + 1);
			next += (len + 2);
		}
	}

	ALuint AudioManager::GetNextAvailableSourceAndBufferIndex()
	{
		for (i32 i = 0; i < NUM_BUFFERS; ++i)
		{
			if (s_Sources[i].source == InvalidAudioSourceID)
			{
				return (ALuint)i;
			}
		}

		return InvalidAudioSourceID;
	}
} // namespace flex
