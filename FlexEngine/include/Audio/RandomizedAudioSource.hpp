#pragma once

namespace flex
{
	class RandomizedAudioSource
	{
	public:
		// Loads several audio source files post-fix numbered starting from "00" going up to fileCount
		void Initialize(const std::string& filePath0, i32 fileCount);
		void Destroy();

		void Play(bool forceRestart);
		void Pause();
		void Stop();

		void SetGain(real volume);
		void SetPitch(real pitch);

		bool IsInitialized() const;
		bool IsPlaying() const;

	private:
		i32 GetRandomIndex();

		bool m_bInitialized = false;

		real m_Pitch = 1.0f;
		real m_Gain = 1.0f;

		std::vector<AudioSourceID> m_SourceIDs;
		AudioSourceID m_LastPlayedID = InvalidAudioSourceID;

	};
} // namespace flex
