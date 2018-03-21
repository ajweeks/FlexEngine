#pragma once

class btDiscreteDynamicsWorld;

namespace flex
{
	struct GameContext;

	class PhysicsWorld
	{
	public:
		PhysicsWorld();
		virtual ~PhysicsWorld();

		void Initialize(const GameContext& gameContext);
		void Destroy();

		void Update(sec deltaSeconds);

		btDiscreteDynamicsWorld* GetWorld();

	private:
		btDiscreteDynamicsWorld * m_World = nullptr;

		static const u32 MAX_SUBSTEPS = 32;

	};

} // namespace flex
