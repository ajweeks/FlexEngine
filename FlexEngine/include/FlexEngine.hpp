#pragma once

#include <string>

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
		void SetupImGuiStyles();

		std::string RenderIDToString(RendererID rendererID) const;

		u32 m_RendererCount;

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
