
#include "stdafx.hpp"

#include "Audio/AudioManager.hpp"

#include "Logger.hpp"
#include "Helpers.hpp"

namespace flex
{
	std::vector<AudioManager::AudioSource> AudioManager::m_Sources;

	ALCcontext* AudioManager::s_Context = nullptr;
	ALCdevice* AudioManager::s_Device = nullptr;

	ALuint AudioManager::m_Buffers[NUM_BUFFERS];

	void AudioManager::Initialize()
	{
		// Retrieve preferred device
		s_Device = alcOpenDevice(NULL);

		if (!s_Device)
		{
			Logger::LogError("Failed to create an openAL device!");
			return;
		}

		PrintAudioDevices(alcGetString(NULL, ALC_DEVICE_SPECIFIER));

		Logger::LogInfo(std::string("Chosen device: ") + alcGetString(s_Device, ALC_DEVICE_SPECIFIER));

		s_Context = alcCreateContext(s_Device, NULL);
		alcMakeContextCurrent(s_Context);

		bool eaxPresent = alIsExtensionPresent("EAX2.0") == GL_TRUE;

		alGenBuffers(NUM_BUFFERS, m_Buffers);
		ALenum error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alGenBuffers: ", error);
			return;
		}
	}

	void AudioManager::Destroy()
	{
		//s_Context = alcGetCurrentContext();
		//s_Device = alcGetContextsDevice(s_Context);
		alcMakeContextCurrent(NULL);
		alcDestroyContext(s_Context);
		alcCloseDevice(s_Device);
	}

	AudioSourceID AudioManager::AddAudioSource(const std::string& filePath)
	{
		AudioSourceID newID = m_Sources.size();
		// TODO: Reuse buffers after sounds stop playing?
		assert(newID < NUM_BUFFERS);


		// WAVE file
		i32 format;
		void* data;
		i32 size;
		i32 freq;
		if (!ParseWAVFile(filePath, &format, &data, &size, &freq))
		{
			Logger::LogError("Failed to open or parse WAVE file");
			return InvalidAudioSourceID;
		}

		ALenum error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("OpenAndParseWAVFile: ", error);
			alDeleteBuffers(NUM_BUFFERS, m_Buffers);
			return InvalidAudioSourceID;
		}


		// Buffer
		alBufferData(m_Buffers[newID], format, data, size, freq);
		error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alBufferData: ", error);
			alDeleteBuffers(NUM_BUFFERS, m_Buffers);
			return InvalidAudioSourceID;
		}
		delete[] data;


		// Source
		m_Sources.resize(m_Sources.size() + 1);

		alGenSources(1, &m_Sources[newID].source);
		error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alGenSources 1: ", error);
			return InvalidAudioSourceID;
		}

		alSourcei(m_Sources[newID].source, AL_BUFFER, m_Buffers[newID]);
		error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alSourcei: ", error);
		}

		return newID;
	}

	void AudioManager::PlayAudioSource(AudioSourceID sourceID)
	{
		assert(sourceID < m_Sources.size());

		alGetSourcei(sourceID, AL_SOURCE_STATE, &m_Sources[sourceID].state);

		if (m_Sources[sourceID].state != AL_PLAYING)
		{
			alSourcePlay(m_Sources[sourceID].source);
			alGetSourcei(sourceID, AL_SOURCE_STATE, &m_Sources[sourceID].state);
		}
	}

	void AudioManager::PauseAudioSource(AudioSourceID sourceID)
	{
		assert(sourceID < m_Sources.size());

		alGetSourcei(sourceID, AL_SOURCE_STATE, &m_Sources[sourceID].state);

		if (m_Sources[sourceID].state != AL_PAUSED)
		{
			alSourcePause(m_Sources[sourceID].source);
			alGetSourcei(sourceID, AL_SOURCE_STATE, &m_Sources[sourceID].state);
		}
	}

	void AudioManager::StopAudioSource(AudioSourceID sourceID)
	{
		assert(sourceID < m_Sources.size());

		alGetSourcei(sourceID, AL_SOURCE_STATE, &m_Sources[sourceID].state);

		if (m_Sources[sourceID].state != AL_STOPPED)
		{
			alSourceStop(m_Sources[sourceID].source);
			alGetSourcei(sourceID, AL_SOURCE_STATE, &m_Sources[sourceID].state);
		}
	}

	void AudioManager::SetAudioSourceGain(AudioSourceID sourceID, real gain)
	{
		assert(sourceID < m_Sources.size());

		gain = glm::clamp(gain, 0.0f, 1.0f);

		m_Sources[sourceID].gain = gain;
		Logger::LogInfo("gain: " + std::to_string(gain));
		alSourcef(m_Sources[sourceID].source, AL_GAIN, gain);
	}

	real AudioManager::GetAudioSourceGain(AudioSourceID sourceID)
	{
		assert(sourceID < m_Sources.size());

		return m_Sources[sourceID].gain;
	}

	void AudioManager::SetAudioSourceLooping(AudioSourceID sourceID, bool looping)
	{
		assert(sourceID < m_Sources.size());

		m_Sources[sourceID].bLooping = looping;
		alSourcei(m_Sources[sourceID].source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
	}

	bool AudioManager::GetAudioSourceLooping(AudioSourceID sourceID)
	{
		assert(sourceID < m_Sources.size());

		return m_Sources[sourceID].bLooping;
	}

	void AudioManager::SetAudioSourcePitch(AudioSourceID sourceID, real pitch)
	{
		assert(sourceID < m_Sources.size());

		// openAL range (found in al.h)
		pitch = glm::clamp(pitch, 0.5f, 2.0f);

		m_Sources[sourceID].pitch = pitch;
		Logger::LogInfo("pitch: " + std::to_string(pitch));
		alSourcef(m_Sources[sourceID].source, AL_PITCH, pitch);
	}

	real AudioManager::GetAudioSourcePitch(AudioSourceID sourceID)
	{
		assert(sourceID < m_Sources.size());

		return m_Sources[sourceID].pitch;
	}

	void AudioManager::DisplayALError(const std::string& str, ALenum error)
	{
		switch (error)
		{
		case AL_INVALID_NAME:		Logger::LogError(str + "Invalid name");
		case AL_ILLEGAL_ENUM:		Logger::LogError(str + "Invalid enum");
		case AL_INVALID_VALUE:		Logger::LogError(str + "Invalid value");
		case AL_INVALID_OPERATION:	Logger::LogError(str + "Invalid operation");
		case AL_OUT_OF_MEMORY:		Logger::LogError(str + "Out of memory");
		default:					Logger::LogError(str + "Unknown error");
		}
	}

	void AudioManager::PrintAudioDevices(const ALCchar* devices)
	{
		const ALCchar* device = devices;
		const ALCchar* next = devices + 1;

		Logger::LogInfo("Available OpenAL devices:");
		while (device && *device != '\0' && 
			   next && *next != '\0')
		{
			Logger::LogInfo('\t' + device);
			size_t len = strlen(device);
			device += (len + 1);
			next += (len + 2);
		}
	}
} // namespace flex
