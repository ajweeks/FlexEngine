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

		/*
		* Returns true if we are now interacting, false 
		* if the object passed in is null or equals our already set interacting object
		*/
		bool SetInteractingWith(GameObject* gameObject);

	private:
		PlayerController* m_Controller = nullptr;
		i32 m_Index = 0;

		real m_MoveFriction = 1.1f;

		GameObject* m_ObjectInteractingWith = nullptr;
	};
} // namespace flex
