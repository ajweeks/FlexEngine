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
	RigidBody::RigidBody()
	{
	}

	RigidBody::~RigidBody()
	{
	}

	void RigidBody::Initialize(btCollisionShape* collisionShape, const GameContext& gameContext, bool isKinematic, bool isStatic)
	{
		btTransform startTransform = btTransform::getIdentity();

		m_MotionState = new btDefaultMotionState(startTransform);
		btRigidBody::btRigidBodyConstructionInfo info = btRigidBody::btRigidBodyConstructionInfo(m_Mass, m_MotionState, collisionShape);

		m_RigidBody = new btRigidBody(info);

		if (isKinematic)
		{
			m_RigidBody->setFlags(btCollisionObject::CF_KINEMATIC_OBJECT);
		}
		if (isStatic)
		{
			m_RigidBody->setFlags(btCollisionObject::CF_STATIC_OBJECT);
		}

		gameContext.sceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->addRigidBody(m_RigidBody);
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

	void RigidBody::GetTransform(glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale)
	{
		btTransform transform;
		m_RigidBody->getMotionState()->getWorldTransform(transform);

		outPos = FromBtVec3(transform.getOrigin());
		outRot = FromBtQuaternion(transform.getRotation());
		outScale = FromBtVec3(m_RigidBody->getCollisionShape()->getLocalScaling());
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
