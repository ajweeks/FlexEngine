#include "stdafx.hpp"

#include "Physics/RigidBody.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletDynamics/ConstraintSolver/btFixedConstraint.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

#include <glm/gtc/matrix_transform.hpp>
IGNORE_WARNINGS_POP

#include "Physics/PhysicsManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/MotionState.hpp"
#include "Scene/SceneManager.hpp"
#include "Transform.hpp"

namespace flex
{
	RigidBody::RigidBody(u32 group, u32 mask) :
		m_Group(group),
		m_Mask(mask)
	{
	}

	RigidBody::RigidBody(const RigidBody& other) :
		m_Mass(other.m_Mass),
		m_bStatic(other.m_bStatic),
		m_bKinematic(other.m_bKinematic),
		m_Friction(other.m_Friction),
		m_Group(other.m_Group),
		m_Mask(other.m_Mask),
		m_Flags(other.m_Flags)
	{
	}

	RigidBody::~RigidBody()
	{
	}

	void RigidBody::Initialize(btCollisionShape* collisionShape, Transform* transform)
	{
		CHECK_EQ(m_btRigidBody, nullptr);
		CHECK_EQ(m_btMotionState, nullptr);

		btVector3 localInertia(0, 0, 0);
		if (!m_bStatic)
		{
			collisionShape->calculateLocalInertia(m_Mass, localInertia);
		}

		if (m_bStatic)
		{
			CHECK_EQ(m_Mass, 0.0f); // Static objects must have a mass of 0!
		}

		m_btMotionState = new MotionState(transform);
		btRigidBody::btRigidBodyConstructionInfo info(m_Mass, m_btMotionState, collisionShape, localInertia);
		m_btRigidBody = new btRigidBody(info);

		i32 flags = m_btRigidBody->getFlags();
		if (m_bKinematic)
		{
			flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
		}
		if (m_bStatic)
		{
			flags |= btCollisionObject::CF_STATIC_OBJECT;
		}
		m_btRigidBody->setFlags(flags);

		m_btRigidBody->setDamping(m_LinearDamping, m_AngularDamping);
		m_btRigidBody->setFriction(m_Friction);

		btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
		world->addRigidBody(m_btRigidBody, (i32)m_Group, (i32)m_Mask);

		m_btRigidBody->getCollisionShape()->setLocalScaling(ToBtVec3(transform->GetWorldScale()));
	}

	void RigidBody::Destroy()
	{
		if (m_btRigidBody != nullptr)
		{
			btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			for (btTypedConstraint* constraint : m_Constraints)
			{
				world->removeConstraint(constraint);
				delete constraint;
			}

			world->removeRigidBody(m_btRigidBody);
			//delete m_btRigidBody->getMotionState(); // ?
			delete m_btRigidBody;
		}

		delete m_btMotionState;
	}

	void RigidBody::AddConstraint(btTypedConstraint* constraint)
	{
		m_btRigidBody->addConstraintRef(constraint);
		m_Constraints.push_back(constraint);

		g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->addConstraint(constraint, true);
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

		if (bKinematic && m_btRigidBody != nullptr)
		{
			m_btRigidBody->clearForces();
			m_btRigidBody->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
			m_btRigidBody->setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));
		}
	}

	bool RigidBody::IsKinematic() const
	{
		return m_bKinematic;
	}

	void RigidBody::SetStatic(bool bStatic)
	{
		m_bStatic = bStatic;
		if (bStatic)
		{
			m_Mass = 0.0f;
		}
	}

	bool RigidBody::IsStatic() const
	{
		return m_bStatic;
	}

	void RigidBody::SetFriction(real friction)
	{
		m_Friction = friction;

		if (m_btRigidBody != nullptr)
		{
			m_btRigidBody->setFriction(friction);
		}
	}

	real RigidBody::GetFriction() const
	{
		return m_Friction;
	}

	void RigidBody::SetLinearDamping(real linearDamping)
	{
		m_LinearDamping = linearDamping;
	}

	void RigidBody::SetAngularDamping(real angularDamping)
	{
		m_AngularDamping = angularDamping;
	}

	void RigidBody::SetLinearFactor(const btVector3& factor)
	{
		m_btRigidBody->setLinearFactor(factor);
	}

	void RigidBody::SetAngularFactor(const btVector3& factor)
	{
		m_btRigidBody->setAngularFactor(factor);
	}

	void RigidBody::SetOrientationConstraint(const btVector3& axis)
	{
		m_btRigidBody->setAngularFactor(axis);
	}

	void RigidBody::SetPositionalConstraint(const btVector3& axis)
	{
		m_btRigidBody->setLinearFactor(axis);
	}

	void RigidBody::SetWorldPosition(const glm::vec3& worldPos)
	{
		if (m_btRigidBody != nullptr)
		{
			m_btRigidBody->setCenterOfMassTransform(btTransform(m_btRigidBody->getCenterOfMassTransform().getRotation(), ToBtVec3(worldPos)));
		}
	}

	void RigidBody::SetWorldRotation(const glm::quat& worldRot)
	{
		if (m_btRigidBody != nullptr)
		{
			m_btRigidBody->setCenterOfMassTransform(btTransform(ToBtQuaternion(worldRot), m_btRigidBody->getCenterOfMassPosition()));
		}
	}

	u32 RigidBody::GetGroup() const
	{
		return m_Group;
	}

	void RigidBody::SetGroup(u32 group)
	{
		m_Group = group;

		btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
		if (m_btRigidBody != nullptr)
		{
			world->removeRigidBody(m_btRigidBody);
			world->addRigidBody(m_btRigidBody, (i32)m_Group, (i32)m_Mask);
		}
	}

	u32 RigidBody::GetMask() const
	{
		return m_Mask;
	}

	void RigidBody::SetMask(u32 mask)
	{
		m_Mask = mask;

		btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
		if (m_btRigidBody != nullptr)
		{
			world->removeRigidBody(m_btRigidBody);
			world->addRigidBody(m_btRigidBody, m_Group, m_Mask);
		}
	}

	//glm::vec3 RigidBody::GetLocalPosition() const
	//{
	//	return m_LocalPosition;
	//}

	//glm::quat RigidBody::GetLocalRotation() const
	//{
	//	return m_LocalRotation;
	//}

	//glm::vec3 RigidBody::GetLocalScale() const
	//{
	//	return m_LocalScale;
	//}

	//void RigidBody::UpdateTransform()
	//{
	//	CHECK_NE(m_Transform, nullptr);

	//	btTransform transform = m_btRigidBody->getWorldTransform();

	//	glm::mat4 worldTransformMat = glm::translate(MAT4_IDENTITY, ToVec3(transform.getOrigin())) *
	//		glm::mat4(ToQuaternion(transform.getRotation())) *
	//		glm::scale(MAT4_IDENTITY, ToVec3(m_btRigidBody->getCollisionShape()->getLocalScaling()));

	//	glm::mat4 childTransformMat = glm::translate(MAT4_IDENTITY, m_LocalPosition) *
	//		glm::mat4(m_LocalRotation) *
	//		glm::scale(MAT4_IDENTITY, m_LocalScale);
	//	glm::mat4 invChildTransformMat = glm::inverse(childTransformMat);

	//	glm::mat4 finalTransformMat = worldTransformMat * invChildTransformMat;

	//	m_Transform->SetWorldFromMatrix(finalTransformMat);
	//}

	//void RigidBody::MatchTransform()
	//{
	//	if (!m_Transform)
	//	{
	//		// NOTE: This should only be true before the first frame
	//		return;
	//	}

	//	glm::mat4 transformMat = glm::translate(MAT4_IDENTITY, m_Transform->GetWorldPosition())
	//		* glm::mat4(m_Transform->GetWorldRotation());

	//	glm::mat4 childTransformMat = glm::translate(MAT4_IDENTITY, m_LocalPosition * m_Transform->GetWorldScale()) *
	//		glm::mat4(m_LocalRotation);

	//	btTransform interpolatedWorldTransform;
	//	m_btRigidBody->getMotionState()->getWorldTransform(interpolatedWorldTransform);


	//	btMatrix3x3 basis = interpolatedWorldTransform.getBasis();
	//	glm::mat4 finalTransformMat = glm::translate(BtMat3ToMat4(basis), ToVec3(interpolatedWorldTransform.getOrigin()));

	//	//= transformMat * childTransformMat;

	//	btTransform transform = btTransform::getIdentity();

	//	//transform.setFromOpenGLMatrix(&finalTransformMat[0][0]);
	//	//m_btRigidBody->setWorldTransform(transform);

	//	if (m_btRigidBody->getCollisionShape())
	//	{
	//		m_btRigidBody->getCollisionShape()->setLocalScaling(ToBtVec3(m_LocalScale * m_Transform->GetWorldScale()));
	//	}

	//	m_btRigidBody->activate();
	//}

	void RigidBody::GetUpRightForward(btVector3& up, btVector3& right, btVector3& forward)
	{
		const btTransform& transform = m_btRigidBody->getWorldTransform();
		btQuaternion rot = transform.getRotation();
		btMatrix3x3 rotMat = btMatrix3x3(rot).transpose();
		right = rotMat[0];
		up = rotMat[1];
		forward = rotMat[2];
	}

	btRigidBody* RigidBody::GetRigidBodyInternal()
	{
		return m_btRigidBody;
	}

	void RigidBody::SetPhysicsFlags(u32 flags)
	{
		m_Flags = flags;
	}

	u32 RigidBody::GetPhysicsFlags()
	{
		return m_Flags;
	}
} // namespace flex
