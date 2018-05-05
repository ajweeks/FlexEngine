#pragma once

namespace flex
{
	class Player;

	class PlayerController final
	{
	public:
		PlayerController();
		~PlayerController();

		void Initialize(Player* player);
		void Update(const GameContext& gameContext);
		void Destroy();

	private:
		real m_MoveSpeed = 800.0f;
		real m_JumpForce = 60.0f;
		real m_RotateSpeed = 500.0f;
		real m_RotateFriction = 4.0f;

		i32 m_PlayerIndex = 0;

		Player* m_Player = nullptr;

	};
} // namespace flex
