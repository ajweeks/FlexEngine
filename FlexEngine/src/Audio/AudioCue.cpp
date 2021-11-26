#include "stdafx.hpp"

#include "Audio/AudioCue.hpp"

#include <stdlib.h> // rand

#include "Audio/AudioManager.hpp"
#include "ResourceManager.hpp"

namespace flex
{
	void AudioCue::Initialize(const std::string& firstFileName, i32 fileCount, bool b2D)
	{
		// "xxx00.wav"
		//     ^
		//     654321
		assert(firstFileName.substr(firstFileName.length() - 6, 2).compare("00") == 0);
		assert(fileCount >= 1);

		// ".wav"
		std::string fileTypeStr = firstFileName.substr(firstFileName.length() - 4);
		// "audio/xxx"
		std::string filePathStripped = firstFileName.substr(0, firstFileName.length() - 6);
		for (i32 i = 0; i < fileCount; ++i)
		{
			std::string iStr = std::to_string(i);
			if (iStr.length() == 1)
			{
				iStr = "0" + iStr;
			}
			std::string filePath = filePathStripped + iStr + fileTypeStr;
			AudioSourceID sourceIDi = g_ResourceManager->GetOrLoadAudioSourceID(SID(filePath.c_str()), b2D);
			m_SourceIDs.push_back(sourceIDi);
		}

		m_bInitialized = true;
	}

	void AudioCue::Destroy()
	{
	}

	void AudioCue::Play(bool bForceRestart)
	{
		if (!bForceRestart && m_LastPlayedID != InvalidAudioSourceID)
		{
			bool stillPlaying = AudioManager::IsSourcePlaying(m_LastPlayedID);
			if (stillPlaying)
			{
				return;
			}
		}
		m_LastPlayedID = m_SourceIDs[GetRandomIndex()];
		AudioManager::PlaySource(m_LastPlayedID, bForceRestart);
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
