#pragma once

#include "Cameras/BaseCamera.hpp"

#include "Transform.hpp"

namespace flex
{
	class Terminal;

	class TerminalCamera final : public BaseCamera
	{
	public:
		explicit TerminalCamera(real FOV = glm::radians(50.0f));

		virtual void Update() override;
		virtual void Initialize() override;

		// Called prior to transitioning in (with a valid Terminal*), or after transitioning out (with nullptr)
		void SetTerminal(Terminal* terminal);

		void TransitionOut();

	private:
		void WrapTargetYaw();

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

		glm::vec3 m_TargetPlayerPos;
		glm::quat m_TargetPlayerRot;

	};
} // namespace flex
