#include "stdafx.hpp"

#pragma warning(push, 0)
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#pragma warning(pop)

#include "Physics/PhysicsManager.hpp"

namespace flex
{
	void PhysicsManager::Initialize()
	{
		if (!m_Initialized)
		{
			m_CollisionConfiguration = new btDefaultCollisionConfiguration();
			m_Dispatcher = new btCollisionDispatcher(m_CollisionConfiguration);
			m_OverlappingPairCache = new btDbvtBroadphase();
			m_Solver = new btSequentialImpulseConstraintSolver;

			m_Initialized = true;
		}
	}

	void PhysicsManager::Destroy()
	{
		if (m_Initialized)
		{
			m_Initialized = false;

			SafeDelete(m_Solver);
			SafeDelete(m_OverlappingPairCache);
			SafeDelete(m_Dispatcher);
			SafeDelete(m_CollisionConfiguration);
		}
	}

	btDiscreteDynamicsWorld* PhysicsManager::CreateWorld()
	{
		if (m_Initialized)
		{
			btDiscreteDynamicsWorld* world = new btDiscreteDynamicsWorld(m_Dispatcher, m_OverlappingPairCache, m_Solver, m_CollisionConfiguration);
			return world;
		}
		return nullptr;
	}
} // namespace flex