#pragma once

#include <utility>
#include <set>

#pragma warning(push, 0)
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <LinearMath/btVector3.h>
#include <LinearMath/btScalar.h>
#pragma warning(pop)

class btDiscreteDynamicsWorld;
class btDynamicsWorld;
class btRigidBody;
class btCollisionObject;

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

		btVector3 GenerateDirectionRayFromScreenPos(const GameContext& gameContext, i32 x, i32 y);

		btRigidBody* PickBody(const btVector3& rayStart, const btVector3& rayEnd, u32 collisionType = 1, const std::string& requiredTag = "");

	private:
		friend void PhysicsInternalTickCallback(btDynamicsWorld *world, btScalar timeStep);

		btDiscreteDynamicsWorld * m_World = nullptr;

		static const u32 MAX_SUBSTEPS = 32;

		std::set<std::pair<const btCollisionObject*, const btCollisionObject*>> m_CollisionPairs;
	};

	void PhysicsInternalTickCallback(btDynamicsWorld *world, btScalar timeStep);
} // namespace flex
