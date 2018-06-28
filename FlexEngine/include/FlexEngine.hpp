#pragma once

#include <string>

#include "GameContext.hpp"
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
		
		GameObject* GetSelectedObject();
		void SetSelectedObject(GameObject* gameObject);

		bool IsDraggingGizmo() const;

		void PreSceneChange();
		void OnSceneChanged();

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

		void DeselectCurrentlySelectedObject();

		// Returns true if the common settings file existed and was valid
		bool LoadCommonSettingsFromDisk();
		void SaveCommonSettingsToDisk();

		u32 m_RendererCount = 0;
		bool m_Running = false;

		GameContext m_GameContext = {};

		RendererID m_RendererIndex = RendererID::_LAST_ELEMENT;
		std::string m_RendererName = "";

		// Indexed using SoundEffect enum
		static std::vector<AudioSourceID> s_AudioSourceIDs;

		GameObject* m_CurrentlySelectedObject;

		GameObject* m_TransformGizmo = nullptr;
		MaterialID m_TransformGizmoMatXID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatYID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatZID = InvalidMaterialID;

		glm::vec3 m_SelectedObjectDragStartPos;
		bool m_bDraggingGizmo = false;
		i32 m_DraggingAxisIndex = -1;

		std::string m_CommonSettingsFileName;
		std::string m_CommonSettingsAbsFilePath;

		FlexEngine(const FlexEngine&) = delete;
		FlexEngine& operator=(const FlexEngine&) = delete;
	};
} // namespace flex
