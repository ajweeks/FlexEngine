#include "stdafx.hpp"

#include "Physics/Collider.hpp"

#pragma warning(push, 0)
#include "btBulletDynamicsCommon.h"
#include "btBulletCollisionCommon.h"
#pragma warning(pop)

#include "Physics/PhysicsWorld.hpp"
#include "GameContext.hpp"
#include "Physics/PhysicsManager.hpp"

namespace flex
{
	Collider::Collider()
	{
	}

	Collider::~Collider()
	{
	}

	btBoxShape* Collider::CreateBoxCollider(const GameContext& gameContext, const btVector3& halfExtents)
	{
		m_Shape = gameContext.physicsManager->CreateBoxShape(halfExtents);
		m_Scale = ((btBoxShape*)m_Shape)->getHalfExtentsWithMargin();
		return (btBoxShape*)m_Shape;
	}

	btSphereShape* Collider::CreateSphereCollider(const GameContext& gameContext, btScalar radius)
	{
		m_Shape = gameContext.physicsManager->CreateSphereShape(radius);
		btScalar margin = m_Shape->getMargin();
		// TODO: Test
		m_Scale = btVector3(radius + margin, radius + margin, radius + margin);
		return (btSphereShape*)m_Shape;
	}

	btCapsuleShape* Collider::CreateCapsuleCollider(const GameContext& gameContext, btScalar radius, btScalar height)
	{
		m_Shape = gameContext.physicsManager->CreateCapsuleShape(radius, height);
		btScalar margin = m_Shape->getMargin();
		// TODO: Test
		m_Scale = btVector3(radius + margin, height + margin, radius + margin);
		return (btCapsuleShape*)m_Shape;
	}

	btCollisionShape* Collider::GetShape()
	{
		return m_Shape;
	}

	btVector3 Collider::GetScale()
	{
		return m_Scale;
	}
} // namespace flex
