#pragma once

#include <set>

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>

#include <LinearMath/btScalar.h>
#include <LinearMath/btVector3.h>
IGNORE_WARNINGS_POP

#include "PhysicsHelpers.hpp"

class btDiscreteDynamicsWorld;
class btDynamicsWorld;
class btRigidBody;
class btCollisionObject;

namespace flex
{
	class PhysicsWorld
	{
	public:
		PhysicsWorld();
		virtual ~PhysicsWorld();

		void Initialize();
		void Destroy();

		void Update(sec deltaSeconds);

		btDiscreteDynamicsWorld* GetWorld();

		btVector3 GenerateDirectionRayFromScreenPos(i32 x, i32 y);

		const btRigidBody* PickFirstBody(const btVector3& rayStart, const btVector3& rayEnd);
		GameObject* PickTaggedBody(const btVector3& rayStart, const btVector3& rayEnd, const std::string& tag, i32 mask = (i32)CollisionType::DEFAULT);

	private:
		friend void PhysicsInternalTickCallback(btDynamicsWorld *world, btScalar timeStep);

		btDiscreteDynamicsWorld* m_World = nullptr;

		static const u32 MAX_SUBSTEPS = 32;

		std::set<std::pair<const btCollisionObject*, const btCollisionObject*>> m_CollisionPairs;
	};

	void PhysicsInternalTickCallback(btDynamicsWorld *world, btScalar timeStep);
} // namespace flex
