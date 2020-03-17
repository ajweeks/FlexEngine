#pragma once

#include "Callbacks/InputCallbacks.hpp"
#include "Spring.hpp"

struct RENDERDOC_API_1_4_0;

struct ImGuiInputTextCallbackData;

namespace flex
{
	class GameObject;
	enum class TransformState;

	class FlexEngine final
	{
	public:
		FlexEngine();
		~FlexEngine();

		void Initialize();
		void UpdateAndRender();
		void Stop();

		i32 ImGuiConsoleInputCallback(ImGuiInputTextCallbackData *data);

		void OnSceneChanged();

		bool IsRenderingImGui() const;

		bool IsRenderingEditorObjects() const;
		void SetRenderingEditorObjects(bool bRenderingEditorObjects);

		bool IsSimulationPaused() const;

		static std::string EngineVersionString();

		static void GenerateRayAtMousePos(btVector3& outRayStart, btVector3& outRayEnd);

		// Returns the intersection point of the given ray & plane, projected on to axis
		static glm::vec3 CalculateRayPlaneIntersectionAlongAxis(
			const glm::vec3& axis,
			const glm::vec3& rayOrigin,
			const glm::vec3& rayEnd,
			const glm::vec3& planeOrigin,
			const glm::vec3& planeNorm,
			const glm::vec3& startPos,
			const glm::vec3& cameraForward,
			real& inOutOffset,
			glm::vec3* outTrueIntersectionPoint = nullptr);

		static const u32 EngineVersionMajor;
		static const u32 EngineVersionMinor;
		static const u32 EngineVersionPatch;

		// TODO: Figure out how to make this not cause a memory leak!
		static std::string s_CurrentWorkingDirectory;

		enum class SoundEffect
		{
			dud_dud_dud_dud,
			drmapan,
			blip,
			synthesized_01,
			synthesized_02,
			synthesized_03,
			synthesized_04,
			synthesized_05,
			synthesized_06,
			synthesized_07,

			LAST_ELEMENT
		};

		static AudioSourceID GetAudioSourceID(SoundEffect effect);

		static bool s_bHasGLDebugExtension;
		u32 mainProcessID = 0;

	private:
		EventReply OnMouseButtonEvent(MouseButton button, KeyAction action);
		MouseButtonCallback<FlexEngine> m_MouseButtonCallback;

		EventReply OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers);
		KeyEventCallback<FlexEngine> m_KeyEventCallback;

		EventReply OnActionEvent(Action action);
		ActionCallback<FlexEngine> m_ActionCallback;

#if COMPILE_RENDERDOC_API
		void SetupRenderDocAPI();
		void CheckForRenderDocUIRunning();
		bool GetLatestRenderDocCaptureFilePath(std::string& outFilePath);
#endif

		void Destroy();

		void CreateWindowAndRenderer();
		void InitializeWindowAndRenderer();
		void DestroyWindowAndRenderer();
		void SetupImGuiStyles();
		void DrawImGuiObjects();

		//void SelectNone();

		// Returns true if the common settings file existed and was valid
		bool LoadCommonSettingsFromDisk();
		void SaveCommonSettingsToDisk(bool bAddEditorStr);

		void AppendToBootupTimesFile(const std::string& entry);

		bool m_bRunning = false;

		bool m_bRenderImGui = true;
		// This value should be set to true on input callbacks
		// to ensure that m_bRenderImGui doesn't change during
		// a single frame, which would cause an uneven number of
		// window pushes/pops
		bool m_bToggleRenderImGui = false;

		bool m_bRenderEditorObjects = true;
		bool m_bUpdateProfilerFrame = false;

		const sec m_MinDT = 0.0001f;
		const sec m_MaxDT = 1.0f;

		bool m_bSimulationPaused = false;
		bool m_bSimulateNextFrame = false;

		real m_SimulationSpeed = 1.0f;

		real m_ImGuiMainWindowWidthMin = 200.0f;
		real m_ImGuiMainWindowWidthMax = 0.0f;
		real m_ImGuiMainWindowWidth = 350.0f;

		std::string m_RendererName;
		std::string m_CompilerName;
		std::string m_CompilerVersion;

		// Indexed using SoundEffect enum
		static std::vector<AudioSourceID> s_AudioSourceIDs;


		std::string m_CommonSettingsFileName;
		std::string m_CommonSettingsAbsFilePath;

		std::string m_BootupTimesFileName;
		std::string m_BootupTimesAbsFilePath;

		std::string m_RenderDocSettingsFileName;
		std::string m_RenderDocSettingsAbsFilePath;

		// Must be stored as member because ImGui will not make a copy
		std::string m_ImGuiIniFilepathStr;
		std::string m_ImGuiLogFilepathStr;

		const real m_SecondsBetweenCommonSettingsFileSave = 10.0f;
		real m_SecondsSinceLastCommonSettingsFileSave = 0.0f;

		bool m_bMainWindowShowing = true;
		bool m_bAssetBrowserShowing = false;
		bool m_bDemoWindowShowing = false;
		bool m_bInputMapperShowing = false;
		bool m_bShowMemoryStatsWindow = false;

		bool m_bWriteProfilerResultsToFile = false;

		struct ConsoleCommand
		{
			typedef void(*cmdFunc)();
			ConsoleCommand(const std::string& name, cmdFunc fun) :
				name(name),
				fun(fun)
			{}

			std::string name;
			cmdFunc fun;
		};
		std::vector<ConsoleCommand> m_ConsoleCommands;
		bool m_bShowingConsole = false;
		static const u32 MAX_CHARS_CMD_LINE_STR = 256;
		char m_CmdLineStrBuf[MAX_CHARS_CMD_LINE_STR];
		i32 m_PreviousCmdLineIndex = -1;
		std::vector<std::string> m_PreviousCmdLineEntries;
		bool m_bShouldFocusKeyboardOnConsole = false;

		std::vector<Spring<glm::vec3>> m_TestSprings;
		real m_SpringTimer = 0.0f;

		std::vector<real> m_FrameTimes;

#if COMPILE_RENDERDOC_API
		RENDERDOC_API_1_4_0* m_RenderDocAPI = nullptr;
		bool m_bRenderDocTriggerCaptureNextFrame = false;
		bool m_bRenderDocCapturingFrame = false;
		i32 m_RenderDocUIPID = -1;
		HMODULE m_RenderDocModule = 0;
		bool m_bShowingRenderDocWindow = false;
		i32 m_RenderDocAPIVerionMajor = -1;
		i32 m_RenderDocAPIVerionMinor = -1;
		i32 m_RenderDocAPIVerionPatch = -1;

		sec m_SecSinceRenderDocPIDCheck = 0.0f;

		i32 m_RenderDocAutoCaptureFrameOffset = -1;
		i32 m_RenderDocAutoCaptureFrameCount = -1;
#endif

		sec SecSinceLogSave = 0.0f;
		sec LogSaveRate = 5.0f;

		FlexEngine(const FlexEngine&) = delete;
		FlexEngine& operator=(const FlexEngine&) = delete;
	};
} // namespace flex
