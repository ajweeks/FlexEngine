#pragma once

#include <string>

#include "InputManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

namespace flex
{
	class GameObject;

	class FlexEngine final
	{
	public:
		FlexEngine();
		~FlexEngine();

		void Initialize();
		void UpdateAndRender();
		void Stop();

		std::vector<GameObject*> GetSelectedObjects();
		void SetSelectedObject(GameObject* gameObject);
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

			LAST_ELEMENT
		};

		static AudioSourceID GetAudioSourceID(SoundEffect effect);

	private:
		enum class RendererID
		{
			VULKAN,
			GL,

			_LAST_ELEMENT
		};

		enum class TransformState
		{
			TRANSLATE,
			ROTATE,
			SCALE
		};

		void Destroy();

		void CycleRenderer();
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

		void DoSceneContextMenu(BaseScene* scene);

		// Returns the intersection point of the given ray & plane, projected on to axis
		glm::vec3 CalculateRayPlaneIntersectionAlongAxis(const glm::vec3& axis,
			const glm::vec3& rayOrigin,
			const glm::vec3& rayEnd,
			const glm::vec3& planeOrigin,
			const glm::vec3& planeNorm);

		glm::quat CalculateDeltaRotationFromGizmoDrag(const glm::vec3& axis,
			const glm::vec3& rayOrigin,
			const glm::vec3& rayEnd,
			const glm::vec3& planeNorm,
			const glm::quat& pRot);

		void UpdateGizmoVisibility();
		void SetTransformState(TransformState state);

		u32 m_RendererCount = 0;
		bool m_bRunning = false;

		bool m_bRenderImGui = true;

		bool m_bRenderEditorObjects = true;
		bool m_bUpdateProfilerFrame = false;

		const sec m_MinDT = 0.0001f;
		const sec m_MaxDT = 1.0f;

		real m_ImGuiMainWindowWidthMin = 200;
		real m_ImGuiMainWindowWidthMax = 0;
		real m_ImGuiMainWindowWidth = 350;

		RendererID m_RendererIndex = RendererID::_LAST_ELEMENT;
		std::string m_RendererName = "";

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
		real m_DraggingGizmoOffset; // How far along the axis the cursor was when pressed
		bool m_bFirstFrameDraggingRotationGizmo = false;
		glm::vec3 m_UnmodifiedAxisProjectedOnto;
		glm::vec3 m_AxisProjectedOnto;
		glm::vec3 m_StartPointOnPlane;
		i32 m_RotationGizmoWrapCount = 0;
		real m_LastAngle;
		real m_pV1oV2;
		glm::vec3 m_PlaneN;
		glm::vec3 m_AxisOfRotation;
		glm::quat m_CurrentRot;
		bool b_LastDotPos = false;

		bool m_bDraggingGizmo = false;
		// -1,   0, 1, 2, 3
		// None, X, Y, Z, All Axes
		i32 m_DraggingAxisIndex = -1;

		std::string m_CommonSettingsFileName;
		std::string m_CommonSettingsAbsFilePath;

		const real m_SecondsBetweenCommonSettingsFileSave = 10.0f;
		real m_SecondsSinceLastCommonSettingsFileSave = 0.0f;

		bool m_bMainWindowShowing = true;
		bool m_bDemoWindowShowing = false;
		bool m_bAssetBrowserShowing = false;

		FlexEngine(const FlexEngine&) = delete;
		FlexEngine& operator=(const FlexEngine&) = delete;
	};
} // namespace flex
