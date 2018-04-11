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
		
		static std::string EngineVersionString();

		static const u32 EngineVersionMajor;
		static const u32 EngineVersionMinor;
		static const u32 EngineVersionPatch;

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
		void LoadDefaultScenes();
		void SetupImGuiStyles();

		std::string RenderIDToString(RendererID rendererID) const;

		u32 m_RendererCount = 0;

		GameContext m_GameContext = {};
		FreeCamera* m_DefaultCamera = nullptr;

		// TODO: Add clear background bool
		glm::vec3 m_ClearColor = { 0,0,0 };
		bool m_VSyncEnabled = true;

		RendererID m_RendererIndex = RendererID::_LAST_ELEMENT;
		std::string m_RendererName = "";

		bool m_Running = false;

		FlexEngine(const FlexEngine&) = delete;
		FlexEngine& operator=(const FlexEngine&) = delete;
	};
} // namespace flex
