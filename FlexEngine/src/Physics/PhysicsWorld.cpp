#include "stdafx.hpp"

#include "Physics/PhysicsWorld.hpp"

#pragma warning(push, 0)
#include <btBulletDynamicsCommon.h>
#pragma warning(pop)

#include "Physics/PhysicsManager.hpp"
#include "GameContext.hpp"

namespace flex
{
	PhysicsWorld::PhysicsWorld()
	{
	}

	PhysicsWorld::~PhysicsWorld()
	{
	}

	void PhysicsWorld::Initialize(const GameContext& gameContext)
	{
		if (!m_World)
		{
			m_World = gameContext.physicsManager->CreateWorld();
			
			//m_World->getDebugDrawer()->setDebugMode(btIDebugDraw::DBG_DrawAabb);
		}
	}

	void PhysicsWorld::Destroy()
	{
		if (m_World)
		{
			for (i32 i = m_World->getNumCollisionObjects() - 1; i >= 0; --i)
			{
				btCollisionObject* pObj = m_World->getCollisionObjectArray()[i];
				btRigidBody* pBody = btRigidBody::upcast(pObj);
				if (pBody && pBody->getMotionState())
				{
					delete pBody->getMotionState();
				}
				m_World->removeCollisionObject(pObj);
				delete pObj;
			}
			
			SafeDelete(m_World);
		}
	}

	void PhysicsWorld::Update(sec deltaSeconds)
	{
		if (m_World)
		{
			m_World->stepSimulation(deltaSeconds, MAX_SUBSTEPS);
		}
	}

	btDiscreteDynamicsWorld* PhysicsWorld::GetWorld()
	{
		return m_World;
	}
} // namespace flex