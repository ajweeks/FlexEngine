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

		void StepSimulation(sec deltaSeconds);

		btDiscreteDynamicsWorld* GetWorld();

		btVector3 GenerateDirectionRayFromScreenPos(i32 x, i32 y);

		// Returns the first body hit along the given ray
		const btRigidBody* PickFirstBody(const btVector3& rayStart, const btVector3& rayEnd);

		// Returns the first body hit along the given ray with the given tag
		GameObject* PickTaggedBody(const btVector3& rayStart, const btVector3& rayEnd, const std::string& tag, i32 mask = (i32)CollisionType::DEFAULT);

		static const u32 MAX_SUBSTEPS;

	private:
		friend void PhysicsInternalTickCallback(btDynamicsWorld *world, btScalar timeStep);

		btDiscreteDynamicsWorld* m_World = nullptr;

		real m_AccumulatedTime = 0.0f;

		std::set<std::pair<const btCollisionObject*, const btCollisionObject*>> m_CollisionPairs;
	};

	void PhysicsInternalTickCallback(btDynamicsWorld *world, btScalar timeStep);


	class CustomContactResultCallback : public btCollisionWorld::ContactResultCallback
	{
	public:
		virtual btScalar addSingleResult(btManifoldPoint& cp, const btCollisionObjectWrapper* colObj0Wrap,
			int partId0, int index0, const btCollisionObjectWrapper* colObj1Wrap, int partId1, int index1) override;

		bool bHit = false;
	};

} // namespace flex
