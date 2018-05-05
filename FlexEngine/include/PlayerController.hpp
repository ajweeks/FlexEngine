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
		real m_MoveSpeed = 1000.0f;
		real m_JumpForce = 200.0f;

		i32 m_PlayerIndex = 0;

		Player* m_Player = nullptr;

	};
} // namespace flex
