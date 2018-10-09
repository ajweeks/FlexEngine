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
		Player(i32 index);
		~Player();

		virtual void Initialize() override;
		virtual void PostInitialize() override;
		virtual void Update() override;
		virtual void Destroy() override;

		i32 GetIndex() const;
		real GetHeight() const;
		PlayerController* GetController();

	private:
		PlayerController* m_Controller = nullptr;
		i32 m_Index = 0;

		GameObject* m_Slingshot = nullptr;
		real m_MoveFriction = 6.0f;
		real m_Height = 4.0f;

		TextureID m_CrosshairTextureID;
	};
} // namespace flex
