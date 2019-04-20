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

		std::vector<GameObject*> GetSelectedObjects();
		void SetSelectedObject(GameObject* gameObject, bool bSelectChildren = true);
		void ToggleSelectedObject(GameObject* gameObject);
		void AddSelectedObject(GameObject* gameObject);
		void DeselectObject(GameObject* gameObject);
		bool IsObjectSelected(GameObject* gameObject);
		glm::vec3 GetSelectedObjectsCenter();
		void CalculateSelectedObjectsCenter();
		void SelectAll();

		bool IsDraggingGizmo() const;

		void PreSceneChange();
		void OnSceneChanged();

		bool IsRenderingImGui() const;

		bool IsRenderingEditorObjects() const;
		void SetRenderingEditorObjects(bool bRenderingEditorObjects);

		bool IsSimulationPaused() const;

		static std::string EngineVersionString();

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

		TransformState GetTransformState() const;
		void SetTransformState(TransformState state);

		bool GetWantRenameActiveElement() const;
		void ClearWantRenameActiveElement();

		static bool s_bHasGLDebugExtension;
		u32 mainProcessID = 0;

	private:
		enum class RendererID
		{
			VULKAN,
			GL,

			_LAST_ELEMENT
		};

		EventReply OnMouseButtonEvent(MouseButton button, KeyAction action);
		MouseButtonCallback<FlexEngine> m_MouseButtonCallback;

		EventReply OnMouseMovedEvent(const glm::vec2& dMousePos);
		MouseMovedCallback<FlexEngine> m_MouseMovedCallback;

		EventReply OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers);
		KeyEventCallback<FlexEngine> m_KeyEventCallback;

		EventReply OnActionEvent(Action action);
		ActionCallback<FlexEngine> m_ActionCallback;

		// True for one frame after the mouse has been released after being pressed at the same location
		glm::vec2i m_LMBDownPos;

#if COMPILE_RENDERDOC_API
		void SetupRenderDocAPI();
#endif

		void Destroy();

		void CreateWindowAndRenderer();
		void InitializeWindowAndRenderer();
		void DestroyWindowAndRenderer();
		void SetupImGuiStyles();
		void DrawImGuiObjects();

		std::string RenderIDToString(RendererID rendererID) const;

		void DeselectCurrentlySelectedObjects();

		// Returns true if the common settings file existed and was valid
		bool LoadCommonSettingsFromDisk();
		void SaveCommonSettingsToDisk(bool bAddEditorStr);

		void AppendToBootupTimesFile(const std::string& entry);

		// Returns the intersection point of the given ray & plane, projected on to axis
		glm::vec3 CalculateRayPlaneIntersectionAlongAxis(const glm::vec3& axis,
			const glm::vec3& rayOrigin,
			const glm::vec3& rayEnd,
			const glm::vec3& planeOrigin,
			const glm::vec3& planeNorm);

		glm::quat CalculateDeltaRotationFromGizmoDrag(const glm::vec3& axis,
			const glm::vec3& rayOrigin,
			const glm::vec3& rayEnd,
			const glm::quat& pRot);

		void UpdateGizmoVisibility();

		// TODO: Convert into more general function which finds the object we're hovering over
		// Checks for mouse hover over gizmo, updates materials & m_HoveringAxisIndex accordingly
		// Only call when mouse is not down!
		// Returns true if gizmo is being hovered over
		bool HandleGizmoHover();
		// Should be called when mouse is released and gizmo is hovered
		void HandleGizmoClick();
		// Converts mouse movement into gizmo (and selected object) movement
		void HandleGizmoMovement();

		// Checks for raycast intersections based on mouse pos
		// Returns true if object was selected or deselected
		bool HandleObjectClick();

		void GenerateRayAtMousePos(btVector3& outRayStart, btVector3& outRayEnd);

		u32 m_RendererCount = 0;
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

		RendererID m_RendererIndex = RendererID::_LAST_ELEMENT;
		std::string m_RendererName;
		std::string m_CompilerName;
		std::string m_CompilerVersion;

		// Indexed using SoundEffect enum
		static std::vector<AudioSourceID> s_AudioSourceIDs;

		std::vector<GameObject*> m_CurrentlySelectedObjects;

		glm::vec3 m_SelectedObjectsCenterPos;
		glm::quat m_SelectedObjectRotation;

		// Parent of translation, rotation, and scale gizmo objects
		GameObject* m_TransformGizmo = nullptr;
		// Children of m_TransformGizmo
		GameObject* m_TranslationGizmo = nullptr;
		GameObject* m_RotationGizmo = nullptr;
		GameObject* m_ScaleGizmo = nullptr;
		MaterialID m_TransformGizmoMatXID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatYID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatZID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatAllID = InvalidMaterialID;

		TransformState m_CurrentTransformGizmoState = TransformState::TRANSLATE;

		std::string m_TranslationGizmoTag = "translation-gizmo";
		std::string m_RotationGizmoTag = "rotation-gizmo";
		std::string m_ScaleGizmoTag = "scale-gizmo";

		glm::vec3 m_SelectedObjectDragStartPos;
		glm::vec3 m_DraggingGizmoScaleLast;
		real m_DraggingGizmoOffset = -1.0f; // How far along the axis the cursor was when pressed
		bool m_bFirstFrameDraggingRotationGizmo = false;
		glm::vec3 m_UnmodifiedAxisProjectedOnto;
		glm::vec3 m_AxisProjectedOnto;
		glm::vec3 m_StartPointOnPlane;
		i32 m_RotationGizmoWrapCount = 0;
		real m_LastAngle = -1.0f;
		real m_pV1oV2= -1.0f;
		glm::vec3 m_PlaneN;
		glm::vec3 m_AxisOfRotation;
		glm::quat m_CurrentRot;
		bool m_bLastDotPos = false;

		bool m_bDraggingGizmo = false;
		// -1,   0, 1, 2, 3
		// None, X, Y, Z, All Axes
		i32 m_DraggingAxisIndex = -1;
		i32 m_HoveringAxisIndex = -1;

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

		bool m_bWriteProfilerResultsToFile = false;

		bool m_bWantRenameActiveElement = false;

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
		i32 m_RenderDocAutoCaptureFrameOffset = -1;
		i32 m_RenderDocAutoCaptureFrameCount = -1;
#endif

		sec SecSinceLogSave = 0.0f;
		sec LogSaveRate = 5.0f;

		FlexEngine(const FlexEngine&) = delete;
		FlexEngine& operator=(const FlexEngine&) = delete;
	};
} // namespace flex
