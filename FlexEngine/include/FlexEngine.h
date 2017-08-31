#pragma once

#include <string>

#include <glm/integer.hpp>

#include "GameContext.h"
#include "FreeCamera.h"
#include "InputManager.h"
#include "Scene/SceneManager.h"
#include "Window/Window.h"

namespace flex
{
	class FlexEngine final
	{
	public:
		FlexEngine();
		~FlexEngine();

		void Initialize();
		void UpdateAndRender();
		void Stop();

	private:
		enum class RendererID
		{
			VULKAN,
			GL,

			_LAST_ELEMENT
		};

		void Destroy();

		void CycleRenderer();
		void InitializeWindowAndRenderer();
		void DestroyWindowAndRenderer();
		void LoadDefaultScenes();

		// ImGui helpers
		void FlexEngine::CopyableColorEdit3(const char* label, glm::vec3& col, const char* copyBtnLabel, const char* pasteBtnLabel, ImGuiColorEditFlags flags = 0);

		std::string RenderIDToString(RendererID rendererID) const;

		glm::uint m_RendererCount;

		Window* m_Window;
		SceneManager* m_SceneManager;
		GameContext m_GameContext;
		FreeCamera* m_DefaultCamera;

		glm::vec3 m_ClearColor;
		bool m_VSyncEnabled;

		RendererID m_RendererIndex;
		std::string m_RendererName;

		bool m_Running;

		FlexEngine(const FlexEngine&) = delete;
		FlexEngine& operator=(const FlexEngine&) = delete;
	};
} // namespace flex
