#include "stdafx.hpp"

#include "Audio/AudioManager.hpp"

#include <imgui/imgui_internal.h> // For PushItemFlag

#include "Helpers.hpp"
#include "Editor.hpp"
#include "ResourceManager.hpp"
#include "StringBuilder.hpp"

namespace flex
{
	std::array<AudioManager::Source, AudioManager::NUM_BUFFERS> AudioManager::s_Sources;

	ALCcontext* AudioManager::s_Context = nullptr;
	ALCdevice* AudioManager::s_Device = nullptr;

	ALuint AudioManager::s_Buffers[NUM_BUFFERS];
	u8* AudioManager::s_WaveData[NUM_BUFFERS];
	u32 AudioManager::s_WaveDataLengths[NUM_BUFFERS];
	u32 AudioManager::s_WaveDataVersions[NUM_BUFFERS];

	real AudioManager::s_MasterGain = 0.2f;
	bool AudioManager::s_Muted = false;

	AudioSourceID AudioManager::s_BeepID = InvalidAudioSourceID;

	SoundClip_Looping::SoundClip_Looping()
	{}

	SoundClip_Looping::SoundClip_Looping(const char* name, AudioSourceID startID, AudioSourceID loopID, AudioSourceID endID) :
		m_Name(name),
		start(startID),
		loop(loopID),
		end(endID),
		m_TimeInStateHisto(128)
	{
	}

	bool SoundClip_Looping::IsValid() const
	{
		return loop != InvalidAudioSourceID;
	}

	bool SoundClip_Looping::IsPlaying() const
	{
		return state != State::OFF;
	}

	void SoundClip_Looping::Start()
	{
		ChangeToState(State::STARTING);
	}

	void SoundClip_Looping::End()
	{
		ChangeToState(State::ENDING);
	}

	void SoundClip_Looping::Update()
	{
		if (loop != InvalidAudioSourceID && state != State::OFF)
		{
			timeInState += g_DeltaTime;
			if (timeInState >= stateLength)
			{
				switch (state)
				{
				case State::STARTING:
				{
					ChangeToState(State::LOOPING);
				} break;
				case State::LOOPING:
				{
					timeInState -= stateLength;
				} break;
				case State::ENDING:
				{
					ChangeToState(State::OFF);
				} break;
				}
			}
		}

		switch (state)
		{
		case State::STARTING:
		{
			m_TimeInStateHisto.AddElement(timeInState);
		} break;
		case State::LOOPING:
		{
			m_TimeInStateHisto.AddElement(1.0f);
		} break;
		case State::ENDING:
		{
			m_TimeInStateHisto.AddElement(1.0f - timeInState);
		} break;
		case State::OFF:
		{
			m_TimeInStateHisto.AddElement(0.0f);
		} break;
		}
	}

	void SoundClip_Looping::ChangeToState(State newState)
	{
		const State oldState = state;

		if (oldState == newState || loop == InvalidAudioSourceID)
		{
			return;
		}

		switch (newState)
		{
		case State::STARTING:
		{
			if (oldState == State::LOOPING)
			{
				PrintError("Unexpected state in SoundClip_Looping: looping -> starting transition\n");
				KillCurrentlyPlaying();
			}

			if (start == InvalidAudioSourceID)
			{
				ChangeToState(State::LOOPING);
				return;
			}

			real offset = -1.0f;

			if (oldState == State::ENDING)
			{
				offset = 1.0f - glm::clamp(timeInState / stateLength, 0.0f, 1.0f);

				AudioManager::FadeSourceOut(end, m_FadeFastDuration, m_FadeFastDuration);
			}

			state = State::STARTING;
			timeInState = 0.0f;
			stateLength = std::max(AudioManager::GetSourceLength(start) - m_FadeDuration, 0.0f);
			if (offset != -1.0f)
			{
				AudioManager::PlaySourceFromPos(start, offset);
			}
			else
			{
				AudioManager::PlaySource(start);
			}
			AudioManager::SetSourceGain(start, 1.0f);
		} break;
		case State::LOOPING:
		{
			if (oldState == State::ENDING)
			{
				PrintError("Unexpected state in SoundClip_Looping: ending -> looping transition\n");
			}

			state = State::LOOPING;
			timeInState = 0.0f;
			stateLength = AudioManager::GetSourceLength(loop);
			AudioManager::PlaySource(loop);
			AudioManager::FadeSourceIn(loop, m_FadeDuration, m_FadeDuration);
		} break;
		case State::ENDING:
		{
			if (end == InvalidAudioSourceID)
			{
				KillCurrentlyPlaying();
				return;
			}

			real offset = -1.0f;

			if (oldState == State::STARTING)
			{
				offset = 1.0f - glm::clamp(timeInState / stateLength, 0.0f, 1.0f);

				AudioManager::FadeSourceOut(start, m_FadeFastDuration, m_FadeFastDuration);
			}
			else if (oldState == State::LOOPING)
			{
				AudioManager::FadeSourceOut(loop, m_FadeDuration, m_FadeDuration);
			}

			state = State::ENDING;
			timeInState = 0.0f;
			stateLength = AudioManager::GetSourceLength(end);
			if (offset != -1.0f)
			{
				AudioManager::PlaySourceFromPos(end, offset);
			}
			else
			{
				AudioManager::PlaySource(end);
			}
			AudioManager::SetSourceGain(end, 1.0f);
		} break;
		case State::OFF:
		{
			KillCurrentlyPlaying();
		} break;
		}
	}

	void SoundClip_Looping::KillCurrentlyPlaying()
	{
		if (loop == InvalidAudioSourceID)
		{
			return;
		}

		switch (state)
		{
		case State::STARTING:
		{
			AudioManager::StopSource(start);
		} break;
		case State::LOOPING:
		{
			AudioManager::StopSource(loop);
		} break;
		case State::ENDING:
		{
			AudioManager::StopSource(end);
		} break;
		}

		state = State::OFF;
		stateLength = -1.0f;
		timeInState = 0.0f;
	}

	void SoundClip_Looping::DrawImGui()
	{
		if (loop == InvalidAudioSourceID)
		{
			ImGui::Text("Invalid");
			return;
		}

		if (ImGui::TreeNode(m_Name))
		{
#define SET_COL(s) if (state == s) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f))
#define SET_COL_POP(s) if (state == s) ImGui::PopStyleColor()

			SET_COL(State::LOOPING);
			ImGui::Text("%s", StateStrings[(i32)State::LOOPING]);
			SET_COL_POP(State::LOOPING);

			SET_COL(State::STARTING);
			ImGui::Text("%s", StateStrings[(i32)State::STARTING]);
			SET_COL_POP(State::STARTING);

			ImGui::SameLine();

			SET_COL(State::ENDING);
			ImGui::Text("%s", StateStrings[(i32)State::ENDING]);
			SET_COL_POP(State::ENDING);

			SET_COL(State::OFF);
			ImGui::Text("%s", StateStrings[(i32)State::OFF]);
			SET_COL_POP(State::OFF);

#undef SET_COl
#undef SET_COl_POP

			ImGui::Text("Time in state: %.2f", timeInState);
			ImGui::Text("State length: %.2f", stateLength);

			ImGui::PlotHistogram("Histo",
				m_TimeInStateHisto.data.data(),
				(i32)m_TimeInStateHisto.data.size(),
				m_TimeInStateHisto.index,
				nullptr,
				0.0f, 1.0f);

			ImGui::TreePop();
		}
	}

	SoundClip_LoopingSimple::SoundClip_LoopingSimple()
	{}

	SoundClip_LoopingSimple::SoundClip_LoopingSimple(const char* name, StringID loopSID) :
		m_Name(name),
		loopSID(loopSID),
		m_VolHisto(128)
	{
		if (loopSID != InvalidStringID)
		{
			if (g_ResourceManager->discoveredAudioFiles.find(loopSID) != g_ResourceManager->discoveredAudioFiles.end())
			{
				if (g_ResourceManager->discoveredAudioFiles[loopSID].sourceID == InvalidAudioSourceID)
				{
					StringBuilder errorStringBuilder;
					g_ResourceManager->LoadAudioFile(loopSID, &errorStringBuilder);
					if (errorStringBuilder.Length() > 0)
					{
						PrintError("Failed to audio file for SoundClip_LoopingSimple, error: %s\n", errorStringBuilder.ToCString());
						return;
					}
				}
				loop = g_ResourceManager->discoveredAudioFiles[loopSID].sourceID;
			}
		}
	}

	bool SoundClip_LoopingSimple::IsValid() const
	{
		return loop != InvalidAudioSourceID;
	}

	bool SoundClip_LoopingSimple::IsPlaying() const
	{
		return bPlaying;
	}

	void SoundClip_LoopingSimple::FadeIn()
	{
		if ((bPlaying && fadeOutTimeRemaining == -1.0f) || (loop == InvalidAudioSourceID))
		{
			return;
		}

		real durationRemaining = 1.0f;
		if (fadeOutTimeRemaining != -1.0f)
		{
			durationRemaining = 1.0f - fadeOutTimeRemaining / m_FadeDuration;
			fadeOutTimeRemaining = -1.0f;
		}

		bPlaying = true;
		AudioManager::PlaySource(loop);
		fadeInTimeRemaining = m_FadeDuration * durationRemaining;
		AudioManager::FadeSourceIn(loop, durationRemaining * m_FadeDuration, m_FadeDuration);
	}

	void SoundClip_LoopingSimple::FadeOut()
	{
		if (bPlaying && fadeOutTimeRemaining == -1.0f && loop != InvalidAudioSourceID)
		{
			real durationRemaining = 1.0f;
			if (fadeInTimeRemaining != -1.0f)
			{
				durationRemaining = glm::clamp(1.0f - fadeInTimeRemaining / m_FadeDuration, 0.0f, 1.0f);
			}

			fadeInTimeRemaining = -1.0f;
			fadeOutTimeRemaining = m_FadeDuration * durationRemaining;
			AudioManager::FadeSourceOut(loop, durationRemaining * m_FadeDuration, m_FadeDuration);
		}
	}

	void SoundClip_LoopingSimple::Update()
	{
		if (loop == InvalidAudioSourceID)
		{
			return;
		}

		if (fadeInTimeRemaining != -1.0f)
		{
			fadeInTimeRemaining -= g_DeltaTime;
			if (fadeInTimeRemaining <= 0.0f)
			{
				fadeInTimeRemaining = -1.0f;
			}
		}
		if (fadeOutTimeRemaining != -1.0f)
		{
			fadeOutTimeRemaining -= g_DeltaTime;
			if (fadeOutTimeRemaining <= 0.0f)
			{
				fadeOutTimeRemaining = -1.0f;
				bPlaying = false;
			}
		}

		if (fadeInTimeRemaining != -1.0f)
		{
			m_VolHisto.AddElement(1.0f - fadeInTimeRemaining / m_FadeDuration);
		}
		else if (fadeOutTimeRemaining != -1.0f)
		{
			m_VolHisto.AddElement(fadeOutTimeRemaining / m_FadeDuration);
		}
		else
		{
			m_VolHisto.AddElement(bPlaying ? 1.0f : 0.0f);
		}
	}

	void SoundClip_LoopingSimple::KillCurrentlyPlaying()
	{
		if (loop != InvalidAudioSourceID)
		{
			AudioManager::StopSource(loop);
		}
		fadeInTimeRemaining = -1.0f;
		fadeOutTimeRemaining = -1.0f;
		bPlaying = false;
	}

	void SoundClip_LoopingSimple::SetPitch(real pitch)
	{
		if (loop != InvalidAudioSourceID)
		{
			AudioManager::SetSourcePitch(loop, pitch);
		}
	}

	void SoundClip_LoopingSimple::SetGainMultiplier(real gainMultiplier)
	{
		AudioManager::SetSourceGainMultiplier(loop, gainMultiplier);
	}

	bool SoundClip_LoopingSimple::DrawImGui()
	{
		bool bTreeOpen = false;
		if (AudioManager::AudioFileNameSIDField(m_Name, loopSID, &bTreeOpen))
		{
			KillCurrentlyPlaying();
			loop = g_ResourceManager->discoveredAudioFiles[loopSID].sourceID;
			if (bTreeOpen)
			{
				ImGui::TreePop();
			}
			return true;
		}

		if (loop == InvalidAudioSourceID)
		{
			if (bTreeOpen)
			{
				ImGui::TreePop();
			}
			return false;
		}

		if (bTreeOpen)
		{
			ImGui::PlotHistogram("Gain",
				m_VolHisto.data.data(),
				(i32)m_VolHisto.data.size(),
				m_VolHisto.index,
				nullptr,
				0.0f, 1.0f);

			ImGui::Text("Gain: %.2f", AudioManager::GetSourceGain(loop));
			ImGui::Text("Pitch: %.2f", AudioManager::GetSourcePitch(loop));
			ImGui::Text("fadeInTimeRemaining: %.2f", fadeInTimeRemaining);
			ImGui::Text("fadeOutTimeRemaining: %.2f", fadeOutTimeRemaining);
			ImGui::Text("bPlaying: %s", (bPlaying ? "true" : "false"));

			ImGui::TreePop();
		}

		return false;
	}

	// ---

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
		s_BeepID = AudioManager::AddAudioSource(SFX_DIRECTORY "wah-wah-02.wav");

		SetMasterGain(s_MasterGain);
	}

	void AudioManager::Destroy()
	{
		ClearAllAudioSources();
		alDeleteBuffers(NUM_BUFFERS, s_Buffers);
		alcMakeContextCurrent(NULL);
		alcDestroyContext(s_Context);
		alcCloseDevice(s_Device);
	}

	void AudioManager::Update()
	{
		for (u32 i = 0; i < (i32)s_Sources.size(); ++i)
		{
			Source& source = s_Sources[i];

			if (source.fadeDurationRemaining > 0.0f)
			{
				real gain;

				source.fadeDurationRemaining -= g_DeltaTime;
				if (source.fadeDurationRemaining < 0.0f)
				{
					source.fadeDurationRemaining = 0.0f;
					source.fadeDuration = 0.0f;
					gain = source.bFadingIn ? 1.0f : 0.0f;
				}
				else if (source.bFadingIn)
				{
					gain = glm::clamp(1.0f - source.fadeDurationRemaining / source.fadeDuration, 0.0f, 1.0f);
				}
				else
				{
					gain = glm::clamp(source.fadeDurationRemaining / source.fadeDuration, 0.0f, 1.0f);
				}
				SetSourceGain((AudioSourceID)i, gain);
			}
		}
	}

	AudioSourceID AudioManager::AddAudioSource(const std::string& filePath, StringBuilder* outErrorStr /* = nullptr */)
	{
		AudioSourceID newID = GetNextAvailableSourceAndBufferIndex();
		if (newID == InvalidAudioSourceID)
		{
			// TODO: Resize buffers dynamically
			PrintError("Failed to add new audio source! All %d buffers are in use\n", NUM_BUFFERS);
			return InvalidAudioSourceID;
		}

		return AddAudioSourceInternal(newID, filePath, outErrorStr);
	}

	AudioSourceID AudioManager::ReplaceAudioSource(const std::string& filePath, AudioSourceID sourceID, StringBuilder* outErrorStr /* = nullptr */)
	{
		if (sourceID == InvalidAudioSourceID)
		{
			PrintError("Attempted to replace Invalid audio source\n");
			return InvalidAudioSourceID;
		}

		StopSource(sourceID);
		bool bSourceWasLooping = GetSourceLooping(sourceID);
		real sourcePitch = GetSourcePitch(sourceID);
		real sourceGain = GetSourceGain(sourceID);
		real sourceGainMultiplier = GetSourceGainMultiplier(sourceID);
		DestroyAudioSource(sourceID);

		AudioSourceID newID = AddAudioSourceInternal(sourceID, filePath, outErrorStr);
		SetSourceLooping(newID, bSourceWasLooping);
		SetSourcePitch(newID, sourcePitch);
		SetSourceGain(newID, sourceGain);
		SetSourceGainMultiplier(newID, sourceGainMultiplier);
		return newID;
	}

	AudioSourceID AudioManager::AddAudioSourceInternal(AudioSourceID sourceID, const std::string& filePath, StringBuilder* outErrorStr)
	{
		if (g_bEnableLogging_Loading)
		{
			const std::string friendlyName = StripLeadingDirectories(filePath);
			Print("Loading audio source %s\n", friendlyName.c_str());
		}

		if (s_WaveData[sourceID] != nullptr)
		{
			delete s_WaveData[sourceID];
		}

		// WAVE file
		i32 format;
		u32 freq;
		StringBuilder sb; // Fallback string builder in case one isn't passed in
		StringBuilder* realSB = (outErrorStr == nullptr ? &sb : outErrorStr);
		if (!ParseWAVFile(filePath, &format, &s_WaveData[sourceID], &s_WaveDataLengths[sourceID], &freq,
			*realSB))
		{
			PrintError("Failed to open or parse WAVE file\n");
			PrintError("%s", realSB->ToCString());
			return InvalidAudioSourceID;
		}

		++s_WaveDataVersions[sourceID];

		// Buffer
		alBufferData(s_Buffers[sourceID], format, s_WaveData[sourceID], s_WaveDataLengths[sourceID], freq);
		ALenum error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alBufferData", error);
			alDeleteBuffers(NUM_BUFFERS, s_Buffers);
			return InvalidAudioSourceID;
		}

		// Source
		alGenSources(1, &s_Sources[sourceID].source);
		error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alGenSources 1", error);
			return InvalidAudioSourceID;
		}

		alSourcei(s_Sources[sourceID].source, AL_BUFFER, s_Buffers[sourceID]);
		DisplayALError("alSourcei", alGetError());

		ALint bufferID, bufferSize, frequency, bitsPerSample, channels;
		alGetSourcei(s_Sources[sourceID].source, AL_BUFFER, &bufferID);
		alGetBufferi(bufferID, AL_SIZE, &bufferSize);
		alGetBufferi(bufferID, AL_FREQUENCY, &frequency);
		alGetBufferi(bufferID, AL_CHANNELS, &channels);
		alGetBufferi(bufferID, AL_BITS, &bitsPerSample);
		s_Sources[sourceID].length = ((real)bufferSize) / (frequency * channels * (bitsPerSample / 8));

		return sourceID;
	}

	void AudioManager::UpdateSourceGain(AudioSourceID sourceID)
	{
		real newGain = Saturate(s_Sources[sourceID].gain * s_Sources[sourceID].gainMultiplier);

		real currGain;
		alGetSourcef(s_Sources[sourceID].source, AL_GAIN, &currGain);
		if (currGain != newGain)
		{
			alSourcef(s_Sources[sourceID].source, AL_GAIN, newGain);

			DisplayALError("SetSourceGain", alGetError());
		}
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
		i32 format = AL_FORMAT_MONO16;
		i32 sampleRate = 44100;
		u32 sampleCount = (i32)(sampleRate * length);
		u32 bufferSize = sampleCount * sizeof(i16);
		i16* data = (i16*)malloc(bufferSize);
		assert(data != nullptr);

		// See http://iquilezles.org/apps/soundtoy/index.html for more patterns
		for (i32 i = 0; i < (i32)sampleCount; ++i)
		{
			real t = (real)i / (real)(sampleCount - 1);
			//t -= fmod(t, 0.5f); // Linear fade in/out
			//t = pow(sin(t* PI), 0.01f); // Sinusodal fade in/out

			real fadePercent = 0.05f;
			real fadeIn = SmoothStep01(glm::clamp(t / fadePercent, 0.0f, 1.0f));
			real fadeOut = SmoothStep01(glm::clamp((1.0f - t) / fadePercent, 0.0f, 1.0f));

			real y = sin(freq * t) * fadeIn * fadeOut;

			//real y = 6.0f * t * exp(-2.0f * t) * sin(freq * t);
			//y *= 0.8f + 0.2f * cos(16.0f * t);

			data[i] = (i16)(y * 32767.0f);
		}

		ALenum error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("OpenAndParseWAVFile", error);
			alDeleteBuffers(NUM_BUFFERS, s_Buffers);
			free(data);
			return InvalidAudioSourceID;
		}


		// Buffer
		alBufferData(s_Buffers[newID], format, data, bufferSize, sampleRate);
		error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("alBufferData", error);
			alDeleteBuffers(NUM_BUFFERS, s_Buffers);
			free(data);
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

		alGetSourcef(s_Sources[newID].source, AL_SEC_OFFSET, &s_Sources[newID].length);

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
		for (u32 id = 0; id < (u32)s_Sources.size(); ++id)
		{
			if (s_Sources[id].source != InvalidAudioSourceID)
			{
				alDeleteSources(1, &s_Sources[id].source);
				delete s_WaveData[id];
			}
		}

		memset(s_WaveData, 0, NUM_BUFFERS * sizeof(u8*));
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

	void AudioManager::PlaySource(AudioSourceID sourceID, bool bForceRestart /* = true */)
	{
		if (sourceID >= s_Sources.size())
		{
			PrintError("Attempted to play invalid source %u\n", (u32)sourceID);
			return;
		}

		// TODO: Just test cached value?
		alGetSourcei(s_Sources[sourceID].source, AL_SOURCE_STATE, &s_Sources[sourceID].state);

		if (bForceRestart || s_Sources[sourceID].state != AL_PLAYING)
		{
			alSourcePlay(s_Sources[sourceID].source);
			alGetSourcei(s_Sources[sourceID].source, AL_SOURCE_STATE, &s_Sources[sourceID].state);
			DisplayALError("PlaySource", alGetError());
		}
	}

	void AudioManager::PlaySourceFromPos(AudioSourceID sourceID, real t)
	{
		if (sourceID >= s_Sources.size())
		{
			PrintError("Attempted to play invalid source %u\n", (u32)sourceID);
			return;
		}

		sec offset = t * s_Sources[sourceID].length;
		alSourcef(s_Sources[sourceID].source, AL_SEC_OFFSET, offset);
		alSourcePlay(s_Sources[sourceID].source);
		alGetSourcei(s_Sources[sourceID].source, AL_SOURCE_STATE, &s_Sources[sourceID].state);
		DisplayALError("PlaySourceFromPos", alGetError());
	}

	void AudioManager::PauseSource(AudioSourceID sourceID)
	{
		if (sourceID >= s_Sources.size())
		{
			PrintError("Attempted to pause invalid source %u\n", (u32)sourceID);
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
			PrintError("Attempted to stop invalid source %u\n", (u32)sourceID);
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

	void AudioManager::FadeSourceIn(AudioSourceID sourceID, real fadeDuration, real fadeMaxDuration)
	{
		s_Sources[sourceID].bFadingIn = true;
		s_Sources[sourceID].fadeDuration = fadeMaxDuration;
		s_Sources[sourceID].fadeDurationRemaining = fadeDuration;
	}

	void AudioManager::FadeSourceOut(AudioSourceID sourceID, real fadeDuration, real fadeMaxDuration)
	{
		s_Sources[sourceID].bFadingIn = false;
		s_Sources[sourceID].fadeDuration = fadeMaxDuration;
		s_Sources[sourceID].fadeDurationRemaining = fadeDuration;
	}

	void AudioManager::SetSourceGain(AudioSourceID sourceID, real gain)
	{
		assert(sourceID < s_Sources.size());
		s_Sources[sourceID].gain = gain;

		UpdateSourceGain(sourceID);
	}

	real AudioManager::GetSourceGain(AudioSourceID sourceID)
	{
		assert(sourceID < s_Sources.size());

		return s_Sources[sourceID].gain;
	}

	void AudioManager::SetSourceGainMultiplier(AudioSourceID sourceID, real gainMultiplier)
	{
		assert(sourceID < s_Sources.size());
		s_Sources[sourceID].gainMultiplier = gainMultiplier;

		UpdateSourceGain(sourceID);
	}

	real AudioManager::GetSourceGainMultiplier(AudioSourceID sourceID)
	{
		assert(sourceID < s_Sources.size());

		return s_Sources[sourceID].gainMultiplier;
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

	real AudioManager::GetSourceLength(AudioSourceID sourceID)
	{
		return s_Sources[sourceID].length;
	}

	u8* AudioManager::GetSourceSamples(AudioSourceID sourceID, u32& outSampleCount, u32& outVersion)
	{
		outSampleCount = s_WaveDataLengths[sourceID];
		outVersion = s_WaveDataVersions[sourceID];
		return s_WaveData[sourceID];
	}

	AudioManager::Source* AudioManager::GetSource(AudioSourceID sourceID)
	{
		return &s_Sources[sourceID];
	}

	real AudioManager::GetSourcePlaybackPos(AudioSourceID sourceID)
	{
		real playbackPos;
		alGetSourcef(s_Sources[sourceID].source, AL_SEC_OFFSET, &playbackPos);
		return playbackPos;
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

	bool AudioManager::AudioFileNameSIDField(const char* label, StringID& sourceFileNameSID, bool* bTreeOpen)
	{
		bool bChanged = false;

		bool bValid = sourceFileNameSID != InvalidStringID;

		ImGui::PushID(label);

		if (bValid)
		{
			const char* sourceName = g_ResourceManager->discoveredAudioFiles[sourceFileNameSID].name.c_str();
			*bTreeOpen = ImGui::TreeNode("audiosourcetree", "%s : %s", label, sourceName);
		}
		else
		{
			ImGui::Text("%s : (null)", label);
		}

		if (bValid && ImGui::BeginPopupContextItem())
		{
			if (ImGui::Button("Clear"))
			{
				sourceFileNameSID = InvalidStringID;
				bValid = false;
				bChanged = true;
			}
			ImGui::EndPopup();
		}

		if (ImGui::IsItemActive())
		{
			if (ImGui::BeginDragDropSource())
			{
				std::string dragDropText = ULongToString(sourceFileNameSID);

				void* data = (void*)&sourceFileNameSID;
				size_t size = sizeof(StringID);

				ImGui::SetDragDropPayload(Editor::AudioFileNameSIDPayloadCStr, data, size);

				ImGui::Text("%s", dragDropText.c_str());

				ImGui::EndDragDropSource();
			}
		}

		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* audioSourcePayload = ImGui::AcceptDragDropPayload(Editor::AudioFileNameSIDPayloadCStr);
			if (audioSourcePayload != nullptr && audioSourcePayload->Data != nullptr)
			{
				StringID draggedSID = *((StringID*)audioSourcePayload->Data);
				sourceFileNameSID = draggedSID;
				bChanged = true;
			}

			ImGui::EndDragDropTarget();
		}

		ImGui::PopID();

		return bChanged;
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
