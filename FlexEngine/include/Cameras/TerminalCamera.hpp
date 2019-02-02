#pragma once

#include "Cameras/BaseCamera.hpp"

#include "Transform.hpp"

namespace flex
{
	class Terminal;

	class TerminalCamera final : public BaseCamera
	{
	public:
		TerminalCamera(real FOV = glm::radians(50.0f), real zNear = 0.1f, real zFar = 1000.0f);
		~TerminalCamera();

		virtual void Update() override;

		void SetTerminal(Terminal* terminal);

	private:
		Terminal* m_Terminal = nullptr;
		glm::vec3 m_TargetPos;
		real m_TargetPitch;
		real m_TargetYaw;

		// Non-serialized fields
		real m_LerpSpeed = 8.0f;
		bool m_bTransitioningIn = false;
		bool m_bTransitioningOut = false;

		glm::vec3 m_StartingPos;
		real m_StartingPitch = -1.0f;
		real m_StartingYaw = -1.0f;

	};
} // namespace flex
