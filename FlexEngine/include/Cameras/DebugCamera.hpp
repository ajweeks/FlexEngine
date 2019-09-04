#pragma once

#include "BaseCamera.hpp"

#include "Callbacks/InputCallbacks.hpp"
#include "Histogram.hpp"

namespace flex
{
	class DebugCamera final : public BaseCamera
	{
	public:
		explicit DebugCamera(real FOV = glm::radians(45.0f));
		~DebugCamera();

		virtual void Initialize() override;
		virtual void Update() override;
		virtual void Destroy() override;
		virtual void DrawImGuiObjects() override;

	private:
		EventReply OnMouseButtonEvent(MouseButton button, KeyAction action);
		MouseButtonCallback<DebugCamera> mouseButtonCallback;

		EventReply OnMouseMovedEvent(const glm::vec2& dMousePos);
		MouseMovedCallback<DebugCamera> mouseMovedCallback;

		real m_RollOnTurnAmount;

		glm::vec2 m_MouseDragDist;

		bool m_bDraggingLMB = false;
		bool m_bDraggingMMB = false;
		bool m_bOrbiting = false;

		glm::vec3 m_DragStartPosition;

		// [0, 1) 0 = no lag, higher values give smoother movement
		real m_MoveLag = 0.5f;
		// TODO: Remove turn lag entirely?
		real m_TurnLag = 0.1f;
		glm::vec3 m_MoveVel;
		glm::vec2 m_TurnVel; // Contains amount pitch and yaw changed last frame

		Histogram m_DragHistory;
	};
} // namespace flex
