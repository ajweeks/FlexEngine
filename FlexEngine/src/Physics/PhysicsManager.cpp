#include "stdafx.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
IGNORE_WARNINGS_POP

#include "Physics/PhysicsManager.hpp"

namespace flex
{
	void PhysicsManager::Initialize()
	{
		PROFILE_AUTO("PhysicsManager Initialize");

		if (!m_bInitialized)
		{
			m_CollisionConfiguration = new btDefaultCollisionConfiguration();
			m_Dispatcher = new btCollisionDispatcher(m_CollisionConfiguration);
			m_OverlappingPairCache = new btDbvtBroadphase();
			m_Solver = new btSequentialImpulseConstraintSolver;

			m_bInitialized = true;
		}
	}

	void PhysicsManager::Destroy()
	{
		if (m_bInitialized)
		{
			m_bInitialized = false;

			delete m_Solver;
			m_Solver = nullptr;
			delete m_OverlappingPairCache;
			m_OverlappingPairCache = nullptr;
			delete m_Dispatcher;
			m_Dispatcher = nullptr;
			delete m_CollisionConfiguration;
			m_CollisionConfiguration = nullptr;
		}
	}

	btDiscreteDynamicsWorld* PhysicsManager::CreateWorld()
	{
		if (m_bInitialized)
		{
			return new btDiscreteDynamicsWorld(m_Dispatcher, m_OverlappingPairCache, m_Solver, m_CollisionConfiguration);
		}
		return nullptr;
	}
} // namespace flex