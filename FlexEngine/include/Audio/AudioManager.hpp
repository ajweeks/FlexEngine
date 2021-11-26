#pragma once

IGNORE_WARNINGS_PUSH
#define AL_ALEXT_PROTOTYPES
#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "AL/efx.h"
#include "AL/efx-presets.h"
IGNORE_WARNINGS_POP

#include "Histogram.hpp"

#include <list>

namespace flex
{
	class StringBuilder;

	struct SoundClip_Looping
	{
		SoundClip_Looping();
		SoundClip_Looping(const char* name, AudioSourceID startID, AudioSourceID loopID, AudioSourceID endID);

		bool IsValid() const;
		bool IsPlaying() const;

		void Start();
		void End();
		void Update();
		void KillCurrentlyPlaying();

		void DrawImGui();

		enum class State
		{
			STARTING,
			LOOPING,
			ENDING,

			OFF
		};

		static constexpr const char* StateStrings[] =
		{
			"Starting",
			"Looping",
			"Ending",
			"Off"
		};

		static_assert(ARRAY_LENGTH(StateStrings) == (u32)State::OFF + 1, "Length of StateStrings must match length of State enum");

		// Optional
		AudioSourceID start = InvalidAudioSourceID;

		AudioSourceID loop = InvalidAudioSourceID;

		// Optional
		AudioSourceID end = InvalidAudioSourceID;

		State state = State::OFF;
		real timeInState = 0.0f;
		real stateLength = -1.0f;

	private:
		void ChangeToState(State newState);

		real m_FadeFastDuration = 0.1f;
		real m_FadeDuration = 0.3f;

		// Debug only
		const char* m_Name = nullptr;
		Histogram m_TimeInStateHisto;

	};

	struct SoundClip_LoopingSimple
	{
		SoundClip_LoopingSimple();
		SoundClip_LoopingSimple(const char* name, StringID loopSID, bool b2D);

		bool IsValid() const;
		bool IsPlaying() const;

		void FadeIn();
		void FadeOut();
		void Update();
		void KillCurrentlyPlaying();

		void SetPitch(real pitch);
		void SetGainMultiplier(real gainMultiplier);

		// Returns true when sound clip has changed
		bool DrawImGui();

		// Serialized value
		StringID loopSID = InvalidStringID;
		// Runtime value
		AudioSourceID loop = InvalidAudioSourceID;

		real fadeInTimeRemaining = -1.0f;
		real fadeOutTimeRemaining = -1.0f;

	private:
		real m_FadeFastDuration = 0.1f;
		real m_FadeDuration = 0.3f;
		bool bPlaying = false;

		// Debug only
		const char* m_Name = "Uninitialized";
		Histogram m_VolHisto;

	};

	struct AudioEffect
	{
		enum class Type : i32
		{
			REVERB,
			CHORUS,
			DISTORTION,
			ECHO,
			FLANGER,
			FREQUENCY_SHIFTER,
			VOCAL_MORPHER,
			PITCH_SHITER,
			RING_MODULATOR,
			AUTOWAH,
			COMPRESSOR,
			EQUALIZER,
			EAX_REVERB,

			// TODO: low/hi/band pass

			_NONE
		};

		EFXEAXREVERBPROPERTIES reverbProperties;
		ALuint effectID;
		Type type;
		i32 presetIndex = -1;
	};

	static const char* AudioEffectTypeStrings[] =
	{
		"Reverb",
		"Chorus",
		"Distortion",
		"Echo",
		"Flanger",
		"Frequency Shifter",
		"Vocal Morpher",
		"Pitch Shifter",
		"Ring Modulator",
		"Auto Wah",
		"Compressor",
		"Equalizer",
		"EAX Reverb",

		"None"
	};

	static_assert(ARRAY_LENGTH(AudioEffectTypeStrings) == (u32)AudioEffect::Type::_NONE + 1, "AudioEffectTypeStrings length must match AudioEffect::Type enum");

	AudioEffect::Type AudioEffectTypeFromALEffect(i32 alEffect);

	const char* AudioEffectTypeToString(AudioEffect::Type type);
	AudioEffect::Type AudioEffectTypeFromString(const char* typeStr);

	// TODO: Make global instance like every other manager?
	// TODO: Inherit from System base class?
	class AudioManager
	{
	public:
		struct Source
		{
			ALuint source = InvalidAudioSourceID;
			real gain = 1.0f;
			real gainMultiplier = 1.0f;
			real pitch = 1.0f;
			real length = -1.0f;

			// AL_INITIAL, AL_PLAYING, AL_PAUSED, or AL_STOPPED
			ALenum state = AL_INITIAL;

			// If non-zero, we're fading in when bFadingIn, and out otherwise
			real fadeDuration = 0.0f;
			real fadeDurationRemaining = 0.0f;
			bool bFadingIn;
			// "2D" sounds remain at a constant volume regardless of listener position
			bool b2D;

			std::string fileName;

			bool bLooping = false;
		};

		static void Initialize();
		static void Destroy();
		static void Update();

		static real NoteToFrequencyHz(i32 note);

		static AudioSourceID SynthesizeSound(sec length, real freq);
		static AudioSourceID SynthesizeMelody(real bpm = 340.0f);
		static bool DestroyAudioSource(AudioSourceID sourceID);
		static void ClearAllAudioSources();

		static void SetListenerPos(const glm::vec3& posWS);
		static void SetListenerVel(const glm::vec3& velocity);

		/* [0.0, 1.0] logarithmic */
		static void SetMasterGain(real masterGain);
		static real GetMasterGain();

		static void PlaySource(AudioSourceID sourceID, bool bForceRestart = true);
		static void PlaySourceWithGain(AudioSourceID sourceID, real gain, bool bForceRestart = true);
		// Start source partway through (t in [0, 1])
		static void PlaySourceAtOffset(AudioSourceID sourceID, real t);
		static void PlaySourceAtPosWS(AudioSourceID sourceID, const glm::vec3& posWS);
		static void PauseSource(AudioSourceID sourceID);
		static void StopSource(AudioSourceID sourceID);
		static void PlayNote(real frequency, sec length, real gain);

		static void SetSourcePositionWS(AudioSourceID sourceID, const glm::vec3& posWS);

		static void MarkSourceTemporary(AudioSourceID sourceID);

		static void FadeSourceIn(AudioSourceID sourceID, real fadeDuration, real fadeMaxDuration);
		static void FadeSourceOut(AudioSourceID sourceID, real fadeDuration, real fadeMaxDuration);

		/* Volume of sound [0.0, 1.0] (logarithmic) */
		static void SetSourceGain(AudioSourceID sourceID, real gain);
		static real GetSourceGain(AudioSourceID sourceID);
		static void SetSourceGainMultiplier(AudioSourceID sourceID, real gainMultiplier);
		static real GetSourceGainMultiplier(AudioSourceID sourceID);
		static bool IsSource2D(AudioSourceID sourceID);

		static void AddToSourcePitch(AudioSourceID sourceID, real deltaPitch);

		/* Pitch of sound [0.5, 2.0] */
		static void SetSourcePitch(AudioSourceID sourceID, real pitch);
		static real GetSourcePitch(AudioSourceID sourceID);

		static void SetSourceLooping(AudioSourceID sourceID, bool bLooping);
		static bool GetSourceLooping(AudioSourceID sourceID);

		static bool IsSourcePlaying(AudioSourceID sourceID);

		static real GetSourceLength(AudioSourceID sourceID);

		static u8* GetSourceSamples(AudioSourceID sourceID, u32& outSampleCount, u32& outVersion);

		static Source* GetSource(AudioSourceID sourceID);
		static std::string GetSourceFileName(AudioSourceID sourceID);

		static real GetSourcePlaybackPos(AudioSourceID sourceID);

		static void ToggleMuted();
		static void SetMuted(bool bMuted);
		static bool IsMuted();

		static void DrawImGuiObjects();

		static bool AudioFileNameSIDField(const char* label, StringID& sourceFileNameSID, bool* bTreeOpen);

		static AudioEffect LoadReverbEffect(const EFXEAXREVERBPROPERTIES* reverb);
		// Returns the created effect type (Either EAX Reverb or normal Reverb)
		static i32 SetupReverbEffect(const EFXEAXREVERBPROPERTIES* reverb, ALuint& effectID);
		static void UpdateReverbEffect(u32 slotID, i32 effectID);

		static i32 SLOT_DEFAULT_2D;
		static i32 SLOT_DEFAULT_3D;

	private:
		friend ResourceManager;

		static AudioSourceID AddAudioSource(const std::string& filePath, StringBuilder* outErrorStr = nullptr, bool b2D = false);
		static AudioSourceID ReplaceAudioSource(const std::string& filePath, AudioSourceID sourceID, StringBuilder* outErrorStr = nullptr);

		static AudioSourceID SynthesizeSoundCommon(AudioSourceID newID, i16* data, u32 bufferSize, u32 sampleRate, i32 format, bool b2D);

		// If error is not AL_NO_ERROR then an appropriate error message is printed
		// Returns true when error != AL_NO_ERROR
		static bool DisplayALError(const char* errorMessage, ALenum error);

		static void PrintAudioDevices(const ALCchar* devices);

		static ALuint GetNextAvailableSourceAndBufferIndex();

		static AudioSourceID AddAudioSourceInternal(AudioSourceID sourceID, const std::string& filePath, StringBuilder* outErrorStr, bool b2D);

		static void UpdateSourceGain(AudioSourceID sourceID);

		static void CreateAudioEffects();
		static void ClearAudioEffects();

		static real s_MasterGain;
		static bool s_Muted;

		static const i32 NUM_BUFFERS = 2048;
		static ALuint s_Buffers[NUM_BUFFERS];

		// Editor-only <
		static u8* s_WaveData[NUM_BUFFERS];
		static u32 s_WaveDataLengths[NUM_BUFFERS];
		// Monotonically increasing value, incrementing each time a new version is loaded
		static u32 s_WaveDataVersions[NUM_BUFFERS];
		// > End Editor-only

		static std::array<Source, NUM_BUFFERS> s_Sources;
		static std::list<AudioSourceID> s_TemporarySources;

		static std::vector<AudioEffect> s_Effects;

		static ALCdevice* s_Device;
		static ALCcontext* s_Context;

		static AudioSourceID s_BeepID;

		AudioManager() = delete;
		~AudioManager() = delete;
	};
} // namespace flex
