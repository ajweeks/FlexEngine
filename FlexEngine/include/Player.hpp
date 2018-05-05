#pragma once

namespace flex
{
	class PlayerController;
	class MeshPrefab;

	class Player
	{
	public:
		Player();
		~Player();

		void Initialize(const GameContext& gameContext, i32 index);
		void Update(const GameContext& gameContext);
		void Destroy(const GameContext& gameContext);

		i32 GetIndex() const;

		RigidBody* GetRigidBody();

	private:
		PlayerController* m_Controller = nullptr;
		i32 m_Index = 0;

		MeshPrefab* m_Mesh = nullptr;
		

	};
} // namespace flex
