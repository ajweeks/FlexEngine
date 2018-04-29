#include "stdafx.hpp"

#include "Physics/RigidBody.hpp"

#pragma warning(push, 0)
#include "btBulletDynamicsCommon.h"
#include "btBulletCollisionCommon.h"
#pragma warning(pop)

#include "Physics/PhysicsWorld.hpp"
#include "GameContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Physics/PhysicsManager.hpp"

namespace flex
{
	RigidBody::RigidBody(i32 group, i32 mask) :
		m_Group(group),
		m_Mask(mask)
	{
	}

	RigidBody::~RigidBody()
	{
	}

	void RigidBody::Initialize(btCollisionShape* collisionShape, const GameContext& gameContext, btTransform& startingTransform)
	{
		btVector3 localInertia(0, 0, 0);
		if (!m_bStatic)
		{
			collisionShape->calculateLocalInertia(m_Mass, localInertia);
		}

		if (m_bStatic)
		{
			assert(m_Mass == 0); // Static objects must have a mass of 0!
		}

		m_MotionState = new btDefaultMotionState(startingTransform);
		btRigidBody::btRigidBodyConstructionInfo info(m_Mass, m_MotionState, collisionShape, localInertia);

		m_RigidBody = new btRigidBody(info);

		i32 flags = m_RigidBody->getFlags();
		if (m_bKinematic)
		{
			flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
		}
		if (m_bStatic)
		{
			flags |= btCollisionObject::CF_STATIC_OBJECT;
		}
		m_RigidBody->setFlags(flags);

		m_RigidBody->setDamping(0.0f, 0.0f);

		gameContext.sceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->addRigidBody(m_RigidBody, m_Group, m_Mask);
	}

	void RigidBody::Destroy(const GameContext& gameContext)
	{
		if (m_RigidBody)
		{
			gameContext.sceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->removeRigidBody(m_RigidBody);
			SafeDelete(m_RigidBody);
		}

		SafeDelete(m_MotionState);
	}

	void RigidBody::SetMass(real mass)
	{
		m_Mass = mass;
	}

	real RigidBody::GetMass() const
	{
		return m_Mass;
	}

	void RigidBody::SetKinematic(bool bKinematic)
	{
		m_bKinematic = bKinematic;
	}

	bool RigidBody::GetKinematic() const
	{
		return m_bKinematic;
	}

	void RigidBody::SetStatic(bool bStatic)
	{
		m_bStatic = bStatic;
	}

	bool RigidBody::GetStatic() const
	{
		return m_bStatic;
	}

	void RigidBody::GetTransform(glm::vec3& outPos, glm::quat& outRot)
	{
		btTransform transform;
		m_RigidBody->getMotionState()->getWorldTransform(transform);

		outPos = FromBtVec3(transform.getOrigin());
		outRot = FromBtQuaternion(transform.getRotation());
	}

	void RigidBody::SetPosition(const glm::vec3& pos)
	{
		btTransform transform;
		m_RigidBody->getMotionState()->getWorldTransform(transform);
		transform.setOrigin(ToBtVec3(pos));
		m_RigidBody->setWorldTransform(transform);
	}

	void RigidBody::SetRotation(const glm::quat& rot)
	{
		btTransform transform;
		m_RigidBody->getMotionState()->getWorldTransform(transform);
		transform.setRotation(ToBtQuaternion(rot));
		m_RigidBody->setWorldTransform(transform);
	}

	void RigidBody::SetScale(const glm::vec3& scale)
	{
		m_RigidBody->getCollisionShape()->setLocalScaling(ToBtVec3(scale));
	}

	btRigidBody* RigidBody::GetRigidBodyInternal()
	{
		return m_RigidBody;
	}

	//glm::vec3 RigidBody::GetPosition()
	//{
	//	btTransform transform;
	//	m_RigidBody->getMotionState()->getWorldTransform(transform);

	//	return FromBtVec3(transform.getOrigin());
	//}

	//glm::quat RigidBody::GetRotation()
	//{
	//	btTransform transform;
	//	m_RigidBody->getMotionState()->getWorldTransform(transform);

	//	return FromBtQuaternion(transform.getRotation());
	//}
} // namespace flex
