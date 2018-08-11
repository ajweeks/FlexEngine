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

		bool IsDraggingGizmo() const;

		void PreSceneChange();
		void OnSceneChanged();

		bool IsRenderingImGui() const;

		bool IsRenderingEditorObjects() const;

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

		u32 m_RendererCount = 0;
		bool m_bRunning = false;

		bool m_bRenderImGui = true;
		u32 m_FrameCount = 0;

		bool m_bRenderEditorObjects = true;
		bool m_bUpdateProfilerFrame = false;

		sec m_MinDT = 0.0001f;
		sec m_MaxDT = 1.0f;

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

		GameObject* m_TransformGizmo = nullptr;
		MaterialID m_TransformGizmoMatXID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatYID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatZID = InvalidMaterialID;

		glm::vec3 m_SelectedObjectDragStartPos;
		bool m_bDraggingGizmo = false;
		i32 m_DraggingAxisIndex = -1;

		std::string m_CommonSettingsFileName;
		std::string m_CommonSettingsAbsFilePath;

		const real m_SecondsBetweenCommonSettingsFileSave = 10.0f;
		real m_SecondsSinceLastCommonSettingsFileSave = 0.0f;

		FlexEngine(const FlexEngine&) = delete;
		FlexEngine& operator=(const FlexEngine&) = delete;
	};
} // namespace flex
