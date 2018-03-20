#include "stdafx.hpp"

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

			auto iter = m_Shapes.begin();
			while (iter != m_Shapes.end())
			{
				delete *iter;
				iter = m_Shapes.erase(iter);
			}
			m_Shapes.clear();

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

	btBoxShape* PhysicsManager::CreateBoxShape(const glm::vec3& halfExtent)
	{
		btBoxShape* box = new btBoxShape(ToBtVec3(halfExtent));
		m_Shapes.push_back(box);
		return box;
	}

	btVector3 ToBtVec3(const glm::vec3& rhs)
	{
		return btVector3(rhs.x, rhs.y, rhs.z);
	}

	btVector4 ToBtVec4(const glm::vec4& rhs)
	{
		return btVector4(rhs.x, rhs.y, rhs.z, rhs.w);
	}

	btQuaternion ToBtQuaternion(const glm::quat& rhs)
	{
		return btQuaternion(rhs.x, rhs.y, rhs.z, rhs.w);
	}
} // namespace flex