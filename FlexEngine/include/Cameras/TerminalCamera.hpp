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
		virtual ~TerminalCamera();

		virtual void Update() override;
		virtual void Initialize() override;

		// Called prior to transitioning in (with a valid Terminal*), or after transitioning out (with nullptr)
		void SetTerminal(Terminal* terminal);

		// Kicks off transition away from this camera
		void TransitionOut();

		bool IsTransitioningIn() const { return m_TransitionRemaining > 0.0f; };
		bool IsTransitioningOut() const { return m_TransitionRemaining < 0.0f; };

	private:
		void WrapTargetYaw();
		void OnLostTerminal();

		GameObjectID m_TerminalID = InvalidGameObjectID;

		// State of the player before transitioning to this camera
		glm::vec3 m_PlayerStartingPos;
		glm::quat m_PlayerStartingRot;

		// Camera state before transitioning to this camera
		glm::vec3 m_StartingPos;
		real m_StartingPitch = -1.0f;
		real m_StartingYaw = -1.0f;

		// Camera state before transitioning to this camera
		glm::vec3 m_TransitionStartingPos;
		real m_TransitionStartingPitch = -1.0f;
		real m_TransitionStartingYaw = -1.0f;

		// Target camera state when transition has completed
		glm::vec3 m_TargetPos;
		real m_TargetPitch = -1.0f;
		real m_TargetYaw = -1.0f;

		// If positive: transitioning in, if negative: transitioning out
		real m_TransitionRemaining = 0.0f;
		real m_CurrentTransitionDuration = 0.0f;
		const real m_TransitionTimePerM = 0.1f;
		const real m_MaxTransitionDuration = 1.0f;

		// How far in front of the terminal the player should be positioned
		const real m_TargetPlayerOffset = 2.0f;

	};
} // namespace flex
