#include "stdafx.hpp"

#include "Physics/RigidBody.hpp"

#pragma warning(push, 0)
#include "btBulletDynamicsCommon.h"
#include "btBulletCollisionCommon.h"
#pragma warning(pop)

#include "GameContext.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Scene/SceneManager.hpp"

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
		m_RigidBody->setFriction(m_Friction);

		gameContext.sceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->addRigidBody(m_RigidBody, m_Group, m_Mask);
	}

	void RigidBody::Destroy(const GameContext& gameContext)
	{
		if (m_RigidBody)
		{
			for (btTypedConstraint* constraint : m_Constraints)
			{
				m_RigidBody->removeConstraintRef(constraint);
				SafeDelete(constraint);
			}

			gameContext.sceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->removeRigidBody(m_RigidBody);
			SafeDelete(m_RigidBody);
		}

		SafeDelete(m_MotionState);
	}

	void RigidBody::AddConstraint(btTypedConstraint* constraint)
	{
		m_RigidBody->addConstraintRef(constraint);
		m_Constraints.push_back(constraint);
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

	bool RigidBody::IsKinematic() const
	{
		return m_bKinematic;
	}

	void RigidBody::SetStatic(bool bStatic)
	{
		m_bStatic = bStatic;
	}

	bool RigidBody::IsStatic() const
	{
		return m_bStatic;
	}

	void RigidBody::SetFriction(real friction)
	{
		m_Friction = friction;

		if (m_RigidBody)
		{
			m_RigidBody->setFriction(friction);
		}
	}

	real RigidBody::GetFriction() const
	{
		return m_Friction;
	}

	void RigidBody::GetTransform(glm::vec3& outPos, glm::quat& outRot)
	{
		btTransform transform;
		m_RigidBody->getMotionState()->getWorldTransform(transform);

		outPos = BtVec3ToVec3(transform.getOrigin());
		outRot = BtQuaternionToQuaternion(transform.getRotation());
	}

	void RigidBody::SetSRT(const glm::vec3& scale, const glm::quat& rot, const glm::vec3& pos)
	{
		btTransform transform;
		m_RigidBody->getMotionState()->getWorldTransform(transform);
		transform.setOrigin(Vec3ToBtVec3(pos));
		transform.setRotation(QuaternionToBtQuaternion(rot));
		m_RigidBody->getCollisionShape()->setLocalScaling(Vec3ToBtVec3(scale));
		m_RigidBody->setWorldTransform(transform);

		m_RigidBody->activate();
	}

	void RigidBody::SetPosition(const glm::vec3& pos)
	{
		btTransform transform;
		m_RigidBody->getMotionState()->getWorldTransform(transform);
		transform.setOrigin(Vec3ToBtVec3(pos));
		m_RigidBody->setWorldTransform(transform);

		m_RigidBody->activate();
	}

	void RigidBody::SetRotation(const glm::quat& rot)
	{
		btTransform transform;
		m_RigidBody->getMotionState()->getWorldTransform(transform);
		transform.setRotation(QuaternionToBtQuaternion(rot));
		m_RigidBody->setWorldTransform(transform);

		m_RigidBody->activate();
	}

	void RigidBody::SetScale(const glm::vec3& scale)
	{
		m_RigidBody->getCollisionShape()->setLocalScaling(Vec3ToBtVec3(scale));

		m_RigidBody->activate();
	}

	void RigidBody::GetUpRightForward(btVector3& up, btVector3& right, btVector3& forward)
	{
		const btVector3 worldUp(0.0f, 1.0f, 0.0f);
		const btVector3 worldRight(1.0f, 0.0f, 0.0f);
		const btVector3 worldForward(0.0f, 0.0f, 1.0f);
		const btTransform& transform = m_RigidBody->getWorldTransform();
		
		btQuaternion rot = transform.getRotation();
		btMatrix3x3 rotMat(rot);
		up = rotMat * worldUp;
		right = rotMat * worldRight;
		forward = rotMat * worldForward;
	}

	btRigidBody* RigidBody::GetRigidBodyInternal()
	{
		return m_RigidBody;
	}

	void RigidBody::SetPhysicsFlags(u32 flags)
	{
		m_Flags = flags;
	}

	u32 RigidBody::GetPhysicsFlags()
	{
		return m_Flags;
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
