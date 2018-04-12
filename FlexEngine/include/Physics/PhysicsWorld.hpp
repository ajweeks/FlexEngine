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

		btVector3 GetRayTo(const GameContext& gameContext, int x, int y);
		bool PickBody(const btVector3& rayFromWorld, const btVector3& rayToWorld);

	private:
		btDiscreteDynamicsWorld * m_World = nullptr;

		static const u32 MAX_SUBSTEPS = 32;

	};

} // namespace flex
