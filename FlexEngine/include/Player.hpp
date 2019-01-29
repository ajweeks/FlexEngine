#pragma once

#include "Scene/GameObject.hpp"

#include "Types.hpp" // For TrackState
#include "Track/BezierCurve.hpp"
#include "Track/BezierCurveList.hpp"

namespace flex
{
	class PlayerController;

	// The player is instructed by its player controller how to move by means of its
	// transform component being updated, and it applies those changes to its rigid body itself
	__declspec(align(16)) class Player : public GameObject
	{
	public:
		Player(i32 index, const glm::vec3& initialPos = VEC3_ZERO);

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Update() override;
		virtual void Destroy() override;
		virtual void DrawImGuiObjects() override;

		void SetPitch(real pitch);
		void AddToPitch(real deltaPitch);
		real GetPitch() const;

		glm::vec3 GetLookDirection() const;

		i32 GetIndex() const;
		real GetHeight() const;
		PlayerController* GetController();

		real GetDistAlongTrack() const;

		void UpdateIsPossessed();

		void ClampPitch();
		void UpdateIsGrounded();

		glm::vec3 GetTrackPlacementReticlePosWS(real snapThreshold = -1.0f, bool bSnapToHandles = false) const;

		void AttachToTrack(TrackID trackID, real distAlongTrack);
		void DetachFromTrack();
		bool IsFacingDownTrack() const;
		void BeginTurnTransition();

		void AddToInventory(GameObject* obj);

		bool IsRidingTrack();

		// TODO: Figure out why this class requires alignment to appease warning C4316
		void* operator new(size_t i);
		void operator delete(void* p);

		PlayerController* m_Controller = nullptr;
		i32 m_Index = 0;

		// TODO: Store IDs rather than raw pointer
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
		TrackID m_TrackEditingID = InvalidTrackID;
		i32 m_TrackEditingCurveIdx = -1;
		i32 m_TrackEditingPointIdx = -1;
		bool m_bEditingTrack = false;
		glm::vec3 m_TrackPlacementReticlePos; // Local offset

		bool m_bGrounded = false;
		bool m_bPossessed = false;

		TrackID m_TrackRidingID = InvalidTrackID;
		real m_DistAlongTrack = 0.0f;
		real m_TrackMoveSpeed = 0.20f;
		real m_pDTrackMovement = 0.0f;

		real m_TrackAttachMinDist = 4.0f;

		TrackState m_TrackState;

		std::vector<GameObject*> m_Inventory;

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
