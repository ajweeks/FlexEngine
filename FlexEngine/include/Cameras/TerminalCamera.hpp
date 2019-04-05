#pragma once

#include "Cameras/BaseCamera.hpp"

#include "Transform.hpp"

namespace flex
{
	class Terminal;

	class TerminalCamera final : public BaseCamera
	{
	public:
		TerminalCamera(real FOV = glm::radians(50.0f));

		virtual void Initialize() override;
		virtual void Update() override;

		// Called prior to transitioning in (with a valid Terminal*), or after transitioning out (with nullptr)
		void SetTerminal(Terminal* terminal);

		void TransitionOut();

	private:
		Terminal* m_Terminal = nullptr;
		glm::vec3 m_TargetPos;
		real m_TargetPitch = -1.0f;
		real m_TargetYaw = -1.0f;

		// Non-serialized fields
		real m_LerpSpeed = 8.0f;
		bool m_bTransitioningIn = false;
		bool m_bTransitioningOut = false;

		glm::vec3 m_StartingPos;
		real m_StartingPitch = -1.0f;
		real m_StartingYaw = -1.0f;

	};
} // namespace flex
