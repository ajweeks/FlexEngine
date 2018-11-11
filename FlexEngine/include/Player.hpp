#pragma once

#include "Scene/GameObject.hpp"

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

		void DrawImGuiObjects();

		// True if going the direction we're facing increases our dist along track value
		bool bFacingForwardDownTrack = true;

	private:
		void ClampPitch();

		PlayerController* m_Controller = nullptr;
		i32 m_Index = 0;

		GameObject* m_MapTablet = nullptr;
		real m_MoveFriction = 12.0f;
		real m_Height = 4.0f;

		real m_Pitch = 0.0f;

		TextureID m_CrosshairTextureID;
	};
} // namespace flex
