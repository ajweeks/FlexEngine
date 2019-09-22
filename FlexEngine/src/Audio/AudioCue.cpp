#include "stdafx.hpp"

#include "Audio/AudioCue.hpp"

#include <stdlib.h> // rand

#include "Audio/AudioManager.hpp"

namespace flex
{
	void AudioCue::Initialize(const std::string& filePath0, i32 fileCount)
	{
		// "xxx00.wav"
		//     ^
		//     654321
		assert(filePath0.substr(filePath0.length() - 6, 2).compare("00") == 0);
		assert(fileCount >= 1);

		// ".wav"
		std::string fileTypeStr = filePath0.substr(filePath0.length() - 4);
		// "audio/xxx"
		std::string filePathStripped = filePath0.substr(0, filePath0.length() - 6);
		for (i32 i = 0; i < fileCount; ++i)
		{
			std::string iStr = std::to_string(i);
			if (iStr.length() == 1)
			{
				iStr = "0" + iStr;
			}
			std::string filePathI = filePathStripped + iStr + fileTypeStr;
			AudioSourceID sourceIDi = AudioManager::AddAudioSource(filePathI);
			m_SourceIDs.push_back(sourceIDi);
		}

		m_bInitialized = true;
	}

	void AudioCue::Destroy()
	{
	}

	void AudioCue::Play(bool forceRestart)
	{
		if (!forceRestart && m_LastPlayedID != InvalidAudioSourceID)
		{
			bool stillPlaying = AudioManager::IsSourcePlaying(m_LastPlayedID);
			if (stillPlaying)
			{
				return;
			}
		}
		m_LastPlayedID = m_SourceIDs[GetRandomIndex()];
		AudioManager::PlaySource(m_LastPlayedID, forceRestart);
	}

	void AudioCue::Pause()
	{
		m_LastPlayedID = m_SourceIDs[GetRandomIndex()];
		AudioManager::PauseSource(m_LastPlayedID);
	}

	void AudioCue::Stop()
	{
		if (m_LastPlayedID != InvalidAudioSourceID)
		{
			AudioManager::StopSource(m_LastPlayedID);
		}
	}

	void AudioCue::SetGain(real gain)
	{
		m_Gain = gain;

		for (AudioSourceID id : m_SourceIDs)
		{
			AudioManager::SetSourceGain(id, gain);
		}
	}

	void AudioCue::SetPitch(real pitch)
	{
		m_Pitch = pitch;

		for (AudioSourceID id : m_SourceIDs)
		{
			AudioManager::SetSourcePitch(id, pitch);
		}
	}

	bool AudioCue::IsInitialized() const
	{
		return m_bInitialized;
	}

	bool AudioCue::IsPlaying() const
	{
		if (m_LastPlayedID == InvalidAudioSourceID)
		{
			return false;
		}
		return AudioManager::IsSourcePlaying(m_LastPlayedID);
	}

	i32 AudioCue::GetRandomIndex()
	{
		// TODO: Use better random function
		// TODO: Don't pick previously chosen ID again
		return (rand() % m_SourceIDs.size());
	}
} // namespace flex
