#include "stdafx.hpp"

#include "Audio/AudioManager.hpp"

#include <imgui/imgui_internal.h> // For PushItemFlag

#include "Helpers.hpp"
#include "Editor.hpp"
#include "ResourceManager.hpp"
#include "StringBuilder.hpp"

namespace flex
{
	real AudioManager::s_MasterGain = 0.2f;
	bool AudioManager::s_Muted = false;

	ALuint AudioManager::s_Buffers[NUM_BUFFERS];
	u8* AudioManager::s_WaveData[NUM_BUFFERS];
	u32 AudioManager::s_WaveDataLengths[NUM_BUFFERS];
	u32 AudioManager::s_WaveDataVersions[NUM_BUFFERS];

	std::array<AudioManager::Source, AudioManager::NUM_BUFFERS> AudioManager::s_Sources;
	std::list<AudioSourceID> AudioManager::s_TemporarySources;

	std::vector<AudioEffect> AudioManager::s_Effects;

	ALCcontext* AudioManager::s_Context = nullptr;
	ALCdevice* AudioManager::s_Device = nullptr;

	AudioSourceID AudioManager::s_BeepID = InvalidAudioSourceID;

	i32 AudioManager::SLOT_DEFAULT_2D = -1;
	i32 AudioManager::SLOT_DEFAULT_3D = -1;

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
				AudioManager::PlaySourceAtOffset(start, offset);
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
				AudioManager::PlaySourceAtOffset(end, offset);
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

	SoundClip_LoopingSimple::SoundClip_LoopingSimple(const char* name, StringID loopSID, bool b2D) :
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
					g_ResourceManager->LoadAudioFile(loopSID, &errorStringBuilder, b2D);
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

	AudioEffect::Type AudioEffectTypeFromALEffect(i32 alEffect)
	{
		switch (alEffect)
		{
		case AL_EFFECT_NULL: return AudioEffect::Type::_NONE;
		case AL_EFFECT_REVERB: return AudioEffect::Type::REVERB;
		case AL_EFFECT_CHORUS: return AudioEffect::Type::CHORUS;
		case AL_EFFECT_DISTORTION: return AudioEffect::Type::DISTORTION;
		case AL_EFFECT_ECHO: return AudioEffect::Type::ECHO;
		case AL_EFFECT_FLANGER: return AudioEffect::Type::FLANGER;
		case AL_EFFECT_FREQUENCY_SHIFTER: return AudioEffect::Type::FREQUENCY_SHIFTER;
		case AL_EFFECT_VOCAL_MORPHER: return AudioEffect::Type::VOCAL_MORPHER;
		case AL_EFFECT_PITCH_SHIFTER: return AudioEffect::Type::PITCH_SHITER;
		case AL_EFFECT_RING_MODULATOR: return AudioEffect::Type::RING_MODULATOR;
		case AL_EFFECT_AUTOWAH: return AudioEffect::Type::AUTOWAH;
		case AL_EFFECT_COMPRESSOR: return AudioEffect::Type::COMPRESSOR;
		case AL_EFFECT_EQUALIZER: return AudioEffect::Type::EQUALIZER;
		case AL_EFFECT_EAXREVERB: return AudioEffect::Type::EAX_REVERB;
		default: return AudioEffect::Type::_NONE;
		}
	}

	const char* AudioEffectTypeToString(AudioEffect::Type type)
	{
		return AudioEffectTypeStrings[(i32)type];
	}

	AudioEffect::Type AudioEffectTypeFromString(const char* typeStr)
	{
		for (i32 i = 0; i < (i32)AudioEffect::Type::_NONE; ++i)
		{
			if (strcmp(typeStr, AudioEffectTypeStrings[i]) == 0)
			{
				return (AudioEffect::Type)i;
			}
		}

		return AudioEffect::Type::_NONE;
	}

	// ---

	void AudioManager::Initialize()
	{
		PROFILE_AUTO("AudioManager Initialize");

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

		alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
		DisplayALError("Distance model", alGetError());

		alDopplerFactor(0.01f);
		DisplayALError("Doppler factor", alGetError());

		u32 slots[2];
		alGenAuxiliaryEffectSlots(2, slots);
		SLOT_DEFAULT_2D = (i32)slots[0];
		SLOT_DEFAULT_3D = (i32)slots[1];

		SetMasterGain(s_MasterGain);

		if (alGetEnumValue("AL_EFFECT_EAXREVERB") != 0)
		{
			Print("Using EAX Reverb\n");
		}
		else
		{
			Print("Using Standard Reverb (EAX not available)\n");
		}

		CreateAudioEffects();

		// Reserve first ID for beep to play on volume change
		s_BeepID = AudioManager::AddAudioSource(SFX_DIRECTORY "wah-wah-02.wav", nullptr, true);

	}

	void AudioManager::Destroy()
	{
		ClearAllAudioSources();
		alDeleteBuffers(NUM_BUFFERS, s_Buffers);

		ClearAudioEffects();

		u32 slots[2] = { (u32)SLOT_DEFAULT_2D, (u32)SLOT_DEFAULT_3D };
		alDeleteAuxiliaryEffectSlots(2, slots);

		alcMakeContextCurrent(NULL);
		alcDestroyContext(s_Context);
		alcCloseDevice(s_Device);
	}

	void AudioManager::Update()
	{
		PROFILE_AUTO("AudioManager Update");

		auto iter = s_TemporarySources.begin();
		while (iter != s_TemporarySources.end())
		{
			if (!IsSourcePlaying(*iter))
			{
				DestroyAudioSource(*iter);
				iter = s_TemporarySources.erase(iter);
			}
			else
			{
				++iter;
			}
		}

		for (u32 i = 0; i < (u32)s_Sources.size(); ++i)
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

	real AudioManager::NoteToFrequencyHz(i32 note)
	{
		const real pitchStandardHz = 440.0f;
		return pitchStandardHz * std::pow(2.0f, (real)(note - 69) / 12.0f);
	}

	AudioSourceID AudioManager::AddAudioSource(const std::string& filePath, StringBuilder* outErrorStr /* = nullptr */, bool b2D /* = false */)
	{
		AudioSourceID newID = GetNextAvailableSourceAndBufferIndex();
		if (newID == InvalidAudioSourceID)
		{
			// TODO: Resize buffers dynamically
			PrintError("Failed to add new audio source! All %d buffers are in use\n", NUM_BUFFERS);
			return InvalidAudioSourceID;
		}

		return AddAudioSourceInternal(newID, filePath, outErrorStr, b2D);
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
		bool b2D = IsSource2D(sourceID);
		DestroyAudioSource(sourceID);

		AudioSourceID newID = AddAudioSourceInternal(sourceID, filePath, outErrorStr, b2D);
		SetSourceLooping(newID, bSourceWasLooping);
		SetSourcePitch(newID, sourcePitch);
		SetSourceGain(newID, sourceGain);
		SetSourceGainMultiplier(newID, sourceGainMultiplier);
		return newID;
	}

	AudioSourceID AudioManager::AddAudioSourceInternal(AudioSourceID sourceID, const std::string& filePath, StringBuilder* outErrorStr, bool b2D)
	{
		std::string fileName = StripLeadingDirectories(filePath);

		if (g_bEnableLogging_Loading)
		{
			Print("Loading audio source %s\n", fileName.c_str());
		}

		if (s_WaveData[sourceID] != nullptr)
		{
			delete s_WaveData[sourceID];
			s_WaveData[sourceID] = nullptr;
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

		if (!b2D && (format == AL_FORMAT_STEREO8 || format == AL_FORMAT_STEREO16))
		{
			PrintWarn("Sound effect is stereo but 3D - only mono sounds will be attenuated\n");
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
			DisplayALError("alGenSources", error);
			return InvalidAudioSourceID;
		}

		alSourcei(s_Sources[sourceID].source, AL_BUFFER, s_Buffers[sourceID]);
		DisplayALError("Sound Buffer", alGetError());

		s_Sources[sourceID].fileName = fileName;

		s_Sources[sourceID].b2D = b2D;
		alSourcei(s_Sources[sourceID].source, AL_SOURCE_RELATIVE, b2D ? AL_TRUE : AL_FALSE);
		DisplayALError("Source Relative", alGetError());

		ALint slot = (ALint)(b2D ? SLOT_DEFAULT_2D : SLOT_DEFAULT_3D);
		alSource3i(s_Sources[sourceID].source, AL_AUXILIARY_SEND_FILTER, slot, 0, AL_FILTER_NULL);
		DisplayALError("Failed to connect to slot", alGetError());

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

	void AudioManager::CreateAudioEffects()
	{
		if (alcIsExtensionPresent(alcGetContextsDevice(alcGetCurrentContext()), "ALC_EXT_EFX"))
		{
			// Load the reverb into an effect
			EFXEAXREVERBPROPERTIES reverb = EFX_REVERB_PRESET_GENERIC;

			AudioEffect effect = AudioManager::LoadReverbEffect(&reverb);
			if (effect.effectID != 0)
			{
				s_Effects.push_back(effect);
				UpdateReverbEffect(SLOT_DEFAULT_3D, (i32)effect.effectID);
			}
		}
		else
		{
			PrintWarn("EFX not supported\n");
		}
	}

	void AudioManager::UpdateReverbEffect(u32 slotID, i32 effectID)
	{
		// Tell the effect slot to use the loaded effect object. Note that the this
		// effectively copies the effect properties. You can modify or delete the
		// effect object afterward without affecting the effect slot.
		alAuxiliaryEffectSloti(slotID, AL_EFFECTSLOT_EFFECT, effectID);
		DisplayALError("Failed to set effect slot", alGetError());
	}

	void AudioManager::ClearAudioEffects()
	{
		for (const AudioEffect& effect : s_Effects)
		{
			alDeleteEffects(1, &effect.effectID);
		}
		s_Effects.clear();
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
		u32 sampleCount = (u32)(sampleRate * length);
		u32 bufferSize = sampleCount * sizeof(i16);
		i16* data = (i16*)malloc(bufferSize);
		assert(data != nullptr);

		// See http://iquilezles.org/apps/soundtoy/index.html for more patterns
		for (u32 i = 0; i < sampleCount; ++i)
		{
			real t = (real)i / (real)(sampleRate);
			real alpha = (real)i / (real)(sampleCount - 1);
			//t -= fmod(t, 0.5f); // Linear fade in/out
			//t = pow(sin(t* PI), 0.01f); // Sinusodal fade in/out

			real fadePercent = 0.05f;
			real fadeIn = SmoothStep01(glm::clamp(alpha / fadePercent, 0.0f, 1.0f));
			real fadeOut = SmoothStep01(glm::clamp((1.0f - alpha) / fadePercent, 0.0f, 1.0f));

			real y = sin(freq * t) * fadeIn * fadeOut;

			//real y = 6.0f * t * exp(-2.0f * t) * sin(freq * t);
			//y *= 0.8f + 0.2f * cos(16.0f * t);

			data[i] = (i16)(y * 32767.0f);
		}

		return SynthesizeSoundCommon(newID, data, bufferSize, sampleRate, format, true);
	}

	AudioSourceID AudioManager::SynthesizeMelody(real bpm /* = 340.0f */)
	{
		const i32 notes[] = {
			88, 86, 78, 78, 80, 80,
			85, 83, 74, 74, 76, 76,
			83, 81, 73, 73, 76, 76,
			81, 81, 81, 81
		};

		AudioSourceID newID = GetNextAvailableSourceAndBufferIndex();
		if (newID == InvalidAudioSourceID)
		{
			PrintError("Failed to add new audio source! All %d buffers are in use\n", NUM_BUFFERS);
			return InvalidAudioSourceID;
		}

		real msCounter = 0.0f;
		real phase = 0.0f;
		real delta = 0.0f;
		i32 noteIndex = 0;
		real noteDurationMs = 60'000.0f / bpm;

		sec lengthSec = noteDurationMs / 1000.0f * ARRAY_LENGTH(notes);
		i32 format = AL_FORMAT_MONO16;
		i32 sampleRate = 44100;
		u32 sampleCount = (i32)(sampleRate * lengthSec);
		u32 bufferSize = sampleCount * sizeof(i16);
		i16* data = (i16*)malloc(bufferSize);
		assert(data != nullptr);

		for (i32 i = 0; i < (i32)sampleCount; ++i)
		{
			real noteFrequency = NoteToFrequencyHz(notes[noteIndex]);
			delta = 2.0f * noteFrequency * (PI / sampleRate);

			// Crude square wave
			real sample = std::copysign(0.1f, std::sin(phase));
			phase = std::fmod(phase + delta, 2.0f * PI);

			data[i] = (i16)(sample * 32767.0f);

			msCounter += 1000.0f / sampleRate;
			if (msCounter >= noteDurationMs)
			{
				msCounter = 0.0f;
				noteIndex = std::min(noteIndex + 1, (i32)ARRAY_LENGTH(notes) - 1);
			}
		}

		return SynthesizeSoundCommon(newID, data, bufferSize, sampleRate, format, true);

	}

	bool AudioManager::DestroyAudioSource(AudioSourceID sourceID)
	{
		if (sourceID >= NUM_BUFFERS || s_Sources[sourceID].source == InvalidAudioSourceID)
		{
			return false;
		}

		alDeleteSources(1, &s_Sources[sourceID].source);
		delete s_WaveData[sourceID];
		s_WaveData[sourceID] = nullptr;
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
		s_TemporarySources.clear();
	}

	void AudioManager::SetListenerPos(const glm::vec3& posWS)
	{
		alListener3f(AL_POSITION, posWS.x, posWS.y, posWS.z);
		DisplayALError("SetListenerPos", alGetError());
	}

	void AudioManager::SetListenerVel(const glm::vec3& velocity)
	{
		alListener3f(AL_VELOCITY, velocity.x, velocity.y, velocity.z);
		DisplayALError("SetListenerVel", alGetError());
	}

	void AudioManager::SetMasterGain(real masterGain)
	{
		s_MasterGain = glm::clamp(masterGain, 0.0f, 1.0f);
		alListenerf(AL_GAIN, s_MasterGain);
		DisplayALError("SetMasterGain", alGetError());
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

	void AudioManager::PlaySourceWithGain(AudioSourceID sourceID, real gain, bool bForceRestart)
	{
		SetSourceGain(sourceID, gain);
		PlaySource(sourceID, bForceRestart);
	}

	void AudioManager::PlaySourceAtOffset(AudioSourceID sourceID, real t)
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
		DisplayALError("PlaySourceAtOffset", alGetError());
	}

	void AudioManager::PlaySourceAtPosWS(AudioSourceID sourceID, const glm::vec3& posWS)
	{
		if (sourceID >= s_Sources.size())
		{
			PrintError("Attempted to play invalid source %u\n", (u32)sourceID);
			return;
		}

		SetSourcePositionWS(sourceID, posWS);

		alSourcePlay(s_Sources[sourceID].source);
		alGetSourcei(s_Sources[sourceID].source, AL_SOURCE_STATE, &s_Sources[sourceID].state);
		DisplayALError("PlaySourceAtPosWS", alGetError());
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

	void AudioManager::PlayNote(real frequency, sec length, real gain)
	{
		AudioSourceID sourceID = AudioManager::SynthesizeSound(length, frequency);
		MarkSourceTemporary(sourceID);
		SetSourceGain(sourceID, gain);
		PlaySource(sourceID);
	}

	void AudioManager::SetSourcePositionWS(AudioSourceID sourceID, const glm::vec3& posWS)
	{
		alSource3f(s_Sources[sourceID].source, AL_POSITION, posWS.x, posWS.y, posWS.z);
		DisplayALError("SetSourcePositionWS", alGetError());
	}

	void AudioManager::MarkSourceTemporary(AudioSourceID sourceID)
	{
		s_TemporarySources.push_back(sourceID);
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

	bool AudioManager::IsSource2D(AudioSourceID sourceID)
	{
		assert(sourceID < s_Sources.size());

		return s_Sources[sourceID].b2D;
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

	std::string AudioManager::GetSourceFileName(AudioSourceID sourceID)
	{
		return s_Sources[sourceID].fileName;
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

	AudioEffect AudioManager::LoadReverbEffect(const EFXEAXREVERBPROPERTIES* reverb)
	{
		ALuint effectID = 0;
		alGenEffects(1, &effectID);

		i32 effectType = SetupReverbEffect(reverb, effectID);

		AudioEffect effect;
		effect.reverbProperties = *reverb;
		effect.effectID = effectID;
		effect.type = AudioEffectTypeFromALEffect(effectType);

		return effect;
	}

	i32 AudioManager::SetupReverbEffect(const EFXEAXREVERBPROPERTIES* reverb, ALuint& effectID)
	{
		bool bEAXSupported = alGetEnumValue("AL_EFFECT_EAXREVERB") != 0;
		if (bEAXSupported)
		{
			// EAX reverb is available
			// Set the EAX effect type then load the reverb properties
			alEffecti(effectID, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);

			alEffectf(effectID, AL_EAXREVERB_DENSITY, reverb->flDensity);
			alEffectf(effectID, AL_EAXREVERB_DIFFUSION, reverb->flDiffusion);
			alEffectf(effectID, AL_EAXREVERB_GAIN, reverb->flGain);
			alEffectf(effectID, AL_EAXREVERB_GAINHF, reverb->flGainHF);
			alEffectf(effectID, AL_EAXREVERB_GAINLF, reverb->flGainLF);
			alEffectf(effectID, AL_EAXREVERB_DECAY_TIME, reverb->flDecayTime);
			alEffectf(effectID, AL_EAXREVERB_DECAY_HFRATIO, reverb->flDecayHFRatio);
			alEffectf(effectID, AL_EAXREVERB_DECAY_LFRATIO, reverb->flDecayLFRatio);
			alEffectf(effectID, AL_EAXREVERB_REFLECTIONS_GAIN, reverb->flReflectionsGain);
			alEffectf(effectID, AL_EAXREVERB_REFLECTIONS_DELAY, reverb->flReflectionsDelay);
			alEffectfv(effectID, AL_EAXREVERB_REFLECTIONS_PAN, reverb->flReflectionsPan);
			alEffectf(effectID, AL_EAXREVERB_LATE_REVERB_GAIN, reverb->flLateReverbGain);
			alEffectf(effectID, AL_EAXREVERB_LATE_REVERB_DELAY, reverb->flLateReverbDelay);
			alEffectfv(effectID, AL_EAXREVERB_LATE_REVERB_PAN, reverb->flLateReverbPan);
			alEffectf(effectID, AL_EAXREVERB_ECHO_TIME, reverb->flEchoTime);
			alEffectf(effectID, AL_EAXREVERB_ECHO_DEPTH, reverb->flEchoDepth);
			alEffectf(effectID, AL_EAXREVERB_MODULATION_TIME, reverb->flModulationTime);
			alEffectf(effectID, AL_EAXREVERB_MODULATION_DEPTH, reverb->flModulationDepth);
			alEffectf(effectID, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, reverb->flAirAbsorptionGainHF);
			alEffectf(effectID, AL_EAXREVERB_HFREFERENCE, reverb->flHFReference);
			alEffectf(effectID, AL_EAXREVERB_LFREFERENCE, reverb->flLFReference);
			alEffectf(effectID, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, reverb->flRoomRolloffFactor);
			alEffecti(effectID, AL_EAXREVERB_DECAY_HFLIMIT, reverb->iDecayHFLimit);
		}
		else
		{
			// EAX reverb not supported
			// Set the standard reverb effect type then load the available reverb properties
			alEffecti(effectID, AL_EFFECT_TYPE, AL_EFFECT_REVERB);

			alEffectf(effectID, AL_REVERB_DENSITY, reverb->flDensity);
			alEffectf(effectID, AL_REVERB_DIFFUSION, reverb->flDiffusion);
			alEffectf(effectID, AL_REVERB_GAIN, reverb->flGain);
			alEffectf(effectID, AL_REVERB_GAINHF, reverb->flGainHF);
			alEffectf(effectID, AL_REVERB_DECAY_TIME, reverb->flDecayTime);
			alEffectf(effectID, AL_REVERB_DECAY_HFRATIO, reverb->flDecayHFRatio);
			alEffectf(effectID, AL_REVERB_REFLECTIONS_GAIN, reverb->flReflectionsGain);
			alEffectf(effectID, AL_REVERB_REFLECTIONS_DELAY, reverb->flReflectionsDelay);
			alEffectf(effectID, AL_REVERB_LATE_REVERB_GAIN, reverb->flLateReverbGain);
			alEffectf(effectID, AL_REVERB_LATE_REVERB_DELAY, reverb->flLateReverbDelay);
			alEffectf(effectID, AL_REVERB_AIR_ABSORPTION_GAINHF, reverb->flAirAbsorptionGainHF);
			alEffectf(effectID, AL_REVERB_ROOM_ROLLOFF_FACTOR, reverb->flRoomRolloffFactor);
			alEffecti(effectID, AL_REVERB_DECAY_HFLIMIT, reverb->iDecayHFLimit);
		}

		// Check if an error occurred, and clean up if so
		if (DisplayALError("Reverb effect creation", alGetError()))
		{
			if (alIsEffect(effectID))
			{
				alDeleteEffects(1, &effectID);
			}
			effectID = 0;
		}

		return bEAXSupported ? AL_EFFECT_EAXREVERB : AL_EFFECT_REVERB;
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

	AudioSourceID AudioManager::SynthesizeSoundCommon(AudioSourceID newID, i16* data, u32 bufferSize, u32 sampleRate, i32 format, bool b2D)
	{
		ALenum error = alGetError();
		if (error != AL_NO_ERROR)
		{
			DisplayALError("?", error);
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
		DisplayALError("alSourcei AL_BUFFER", alGetError());

		s_Sources[newID].b2D = b2D;
		alSourcei(s_Sources[newID].source, AL_SOURCE_RELATIVE, b2D ? AL_TRUE : AL_FALSE);
		DisplayALError("Source Relative", alGetError());

		alGetSourcef(s_Sources[newID].source, AL_SEC_OFFSET, &s_Sources[newID].length);

		alSource3i(s_Sources[newID].source, AL_AUXILIARY_SEND_FILTER, SLOT_DEFAULT_2D, 0, AL_FILTER_NULL);
		DisplayALError("Failed to connect to slot", alGetError());

		return newID;
	}

	bool AudioManager::DisplayALError(const char* errorMessage, ALenum error)
	{
		switch (error)
		{
		case AL_NO_ERROR:			return false;
		case AL_INVALID_NAME:		PrintError("ALError: Invalid name - %s\n", errorMessage); break;
		case AL_ILLEGAL_ENUM:		PrintError("ALError: Invalid enum - %s\n", errorMessage); break;
		case AL_INVALID_VALUE:		PrintError("ALError: Invalid value - %s\n", errorMessage); break;
		case AL_INVALID_OPERATION:	PrintError("ALError: Invalid operation - %s\n", errorMessage); break;
		case AL_OUT_OF_MEMORY:		PrintError("ALError: Out of memory - %s\n", errorMessage); break;
		default:					PrintError("ALError: Unknown error - %s\n", errorMessage); break;
		}

		return true;
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
