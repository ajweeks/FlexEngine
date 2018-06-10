#pragma once

#include "Scene/GameObject.hpp"

namespace flex
{
	class PlayerController;

	class Player : public GameObject
	{
	public:
		Player(i32 index);
		~Player();

		virtual void Initialize(const GameContext& gameContext) override;
		virtual void PostInitialize(const GameContext& gameContext) override;
		virtual void Update(const GameContext& gameContext) override;
		virtual void Destroy(const GameContext& gameContext) override;

		i32 GetIndex() const;

		real GetHeight() const;

	private:
		PlayerController* m_Controller = nullptr;
		i32 m_Index = 0;

		GameObject* m_Slingshot = nullptr;

		real m_MoveFriction = 3.0f;

		real m_Height = 4.0f;
	};
} // namespace flex
