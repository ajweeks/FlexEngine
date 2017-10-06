#pragma once

#include <string>

#include <glm/integer.hpp>

#include "GameContext.hpp"
#include "FreeCamera.hpp"
#include "InputManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Window/Window.hpp"

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

		std::string RenderIDToString(RendererID rendererID) const;

		glm::uint m_RendererCount;

		Window* m_Window;
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
