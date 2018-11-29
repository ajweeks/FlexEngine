#pragma once

#include "Scene/GameObject.hpp"

#include "Helpers.hpp"
#include "Track/BezierCurve.hpp"
#include "Track/BezierCurveList.hpp"

namespace flex
{
	class PlayerController;

	// The player is instructed by its player controller how to move by means of its
	// transform component being updated, and it applies those changes to its rigid body itself
	class Player : public GameObject
	{
	public:
		Player(i32 index, const glm::vec3& initialPos = VEC3_ZERO);
		~Player();

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Update() override;
		virtual void Destroy() override;

		void SetPitch(real pitch);
		void AddToPitch(real deltaPitch);
		real GetPitch() const;

		glm::vec3 GetLookDirection() const;

		i32 GetIndex() const;
		real GetHeight() const;
		PlayerController* GetController();

		real GetDistAlongTrack() const;

		void DrawImGuiObjects();
		void UpdateIsPossessed();

		void ClampPitch();
		void UpdateIsGrounded();

		glm::vec3 GetTrackPlacementReticlePosWS(real snapThreshold = -1.0f) const;

		PlayerController* m_Controller = nullptr;
		i32 m_Index = 0;

		GameObject* m_MapTablet = nullptr;
		GameObject* m_MapTabletHolder = nullptr;
		deg m_TabletOrbitAngleUp = 13.3f;
		deg m_TabletOrbitAngleDown = -45.0f;
		real m_TabletOrbitAngle = m_TabletOrbitAngleUp;
		bool m_bTabletUp = true;

		real m_MoveFriction = 12.0f;
		real m_Height = 4.0f;

		real m_Pitch = 0.0f;

		i32 m_CurveNodesPlaced = 0;
		BezierCurveList m_TrackPlacing;
		BezierCurve m_CurvePlacing;
		bool m_bPlacingTrack = false;
		glm::vec3 m_TrackPlacementReticlePos; // Local offset


		bool m_bGrounded = false;
		bool m_bPossessed = false;

		BezierCurveList* m_TrackRiding = nullptr;
		real m_DistAlongTrack = 0.0f;
		real m_TrackMoveSpeed = 0.20f;
		real m_pDTrackMovement = 0.0f;
		bool m_bUpdateFacingAndForceFoward = false;

		real m_TrackAttachMinDist = 4.0f;

		TurningDir m_DirTurning = TurningDir::NONE;

		sec m_SecondsAttemptingToTurn = 0.0f;
		// How large the joystick x value must be to enter a turning state
		const real m_TurnStartStickXThreshold = 0.15f;
		// How large the dot product between our forward and the track forward must be to turn around
		const real m_MinForDotTurnThreshold = 0.03f;
		const sec m_AttemptToTurnTimeThreshold = 0.2f;
		// How long after completing a turn around the player can start accumulating turn time again
		const sec m_TurnAroundCooldown = 0.5f;

		glm::vec3 m_TargetTrackFor = VEC3_FORWARD;

		bool m_bFacingForwardDownTrack = true;
		bool m_bTargetFacingForwardDownTrack = false;

		const real m_TurnToFaceDownTrackInvSpeed = 30.0f;
		const real m_FlipTrackDirInvSpeed = 45.0f;

	private:
		friend class PlayerController;

		AudioSourceID m_SoundPlaceTrackNodeID = InvalidAudioSourceID;
		AudioSourceID m_SoundPlaceFinalTrackNodeID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackAttachID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackDetachID = InvalidAudioSourceID;
		AudioSourceID m_SoundTrackSwitchDirID = InvalidAudioSourceID;

		TextureID m_CrosshairTextureID = InvalidTextureID;
	};
} // namespace flex
