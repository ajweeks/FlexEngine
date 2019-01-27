#include "stdafx.hpp"

#include "Physics/RigidBody.hpp"

#pragma warning(push, 0)
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletDynamics/ConstraintSolver/btFixedConstraint.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

#include <LinearMath/btDefaultMotionState.h>

#include <glm/gtc/matrix_transform.hpp>
#pragma warning(pop)

#include "Physics/PhysicsManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	RigidBody::RigidBody(i32 group, i32 mask) :
		m_LocalPosition(0.0f),
		m_LocalRotation(QUAT_UNIT),
		m_LocalScale(1.0f),
		m_Group(group),
		m_Mask(mask)
	{
	}

	RigidBody::RigidBody(const RigidBody& other) :
		m_ParentTransform(nullptr),
		m_LocalPosition(other.m_LocalPosition),
		m_LocalRotation(other.m_LocalRotation),
		m_LocalScale(other.m_LocalScale),
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

	void RigidBody::Initialize(btCollisionShape* collisionShape, Transform* parentTransform)
	{
		m_ParentTransform = parentTransform;

		btVector3 localInertia(0, 0, 0);
		if (!m_bStatic)
		{
			collisionShape->calculateLocalInertia(m_Mass, localInertia);
		}

		if (m_bStatic)
		{
			assert(m_Mass == 0); // Static objects must have a mass of 0!
		}

		btTransform startingTransform = ToBtTransform(*parentTransform);
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

		m_RigidBody->setDamping(m_LinearDamping, m_AngularDamping);
		m_RigidBody->setFriction(m_Friction);

		btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
		world->addRigidBody(m_RigidBody, m_Group, m_Mask);

		m_RigidBody->getCollisionShape()->setLocalScaling(ToBtVec3(parentTransform->GetWorldScale()));
	}

	void RigidBody::Destroy()
	{
		if (m_RigidBody)
		{
			for (btTypedConstraint* constraint : m_Constraints)
			{
				m_RigidBody->removeConstraintRef(constraint);
				SafeDelete(constraint);
			}

			g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld()->removeRigidBody(m_RigidBody);
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

		if (bKinematic && m_RigidBody)
		{
			m_RigidBody->clearForces();
			m_RigidBody->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
			m_RigidBody->setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));
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

		if (m_RigidBody)
		{
			m_RigidBody->setFriction(friction);
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

	void RigidBody::SetOrientationConstraint(const btVector3& axis)
	{
		m_RigidBody->setAngularFactor(axis);
	}

	void RigidBody::SetPositionalConstraint(const btVector3& axis)
	{
		m_RigidBody->setLinearFactor(axis);
	}

	void RigidBody::SetLocalSRT(const glm::vec3& scale, const glm::quat& rot, const glm::vec3& pos)
	{
		m_LocalPosition = pos;
		m_LocalRotation = rot;
		m_LocalScale = scale;

		MatchParentTransform();
	}

	void RigidBody::SetLocalPosition(const glm::vec3& pos)
	{
		m_LocalPosition = pos;

		MatchParentTransform();
	}

	void RigidBody::SetLocalRotation(const glm::quat& rot)
	{
		m_LocalRotation = rot;

		MatchParentTransform();
	}

	void RigidBody::SetLocalScale(const glm::vec3& scale)
	{
		m_LocalScale = scale;

		MatchParentTransform();
	}

	i32 RigidBody::GetGroup() const
	{
		return m_Group;
	}

	void RigidBody::SetGroup(i32 group)
	{
		m_Group = group;

		btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
		if (m_RigidBody)
		{
			world->removeRigidBody(m_RigidBody);
			world->addRigidBody(m_RigidBody, m_Group, m_Mask);
		}
	}

	i32 RigidBody::GetMask() const
	{
		return m_Mask;
	}

	void RigidBody::SetMask(i32 mask)
	{
		m_Mask = mask;

		btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
		if (m_RigidBody)
		{
			world->removeRigidBody(m_RigidBody);
			world->addRigidBody(m_RigidBody, m_Group, m_Mask);
		}
	}

	glm::vec3 RigidBody::GetLocalPosition() const
	{
		return m_LocalPosition;
	}

	glm::quat RigidBody::GetLocalRotation() const
	{
		return m_LocalRotation;
	}

	glm::vec3 RigidBody::GetLocalScale() const
	{
		return m_LocalScale;
	}

	void RigidBody::UpdateParentTransform()
	{
		assert(m_ParentTransform);

		btTransform transform = m_RigidBody->getWorldTransform();

		glm::mat4 worldTransformMat = glm::translate(MAT4_IDENTITY, ToVec3(transform.getOrigin())) *
			glm::mat4(ToQuaternion(transform.getRotation())) *
			glm::scale(MAT4_IDENTITY, ToVec3(m_RigidBody->getCollisionShape()->getLocalScaling()));

		glm::mat4 childTransformMat = glm::translate(MAT4_IDENTITY, m_LocalPosition) *
			glm::mat4(m_LocalRotation) *
			glm::scale(MAT4_IDENTITY, m_LocalScale);
		glm::mat4 invChildTransformMat = glm::inverse(childTransformMat);

		glm::mat4 finalTransformMat = worldTransformMat * invChildTransformMat;

		m_ParentTransform->SetWorldFromMatrix(finalTransformMat);
	}

	void RigidBody::MatchParentTransform()
	{
		if (!m_ParentTransform)
		{
			// NOTE: This should only be true before the first frame
			return;
		}

		glm::mat4 parentTransformMat = glm::translate(MAT4_IDENTITY, m_ParentTransform->GetWorldPosition())
			* glm::mat4(m_ParentTransform->GetWorldRotation());

		glm::mat4 childTransformMat = glm::translate(MAT4_IDENTITY, m_LocalPosition) *
			glm::mat4(m_LocalRotation);

		glm::mat4 finalTransformMat = parentTransformMat * childTransformMat;

		btTransform transform = btTransform::getIdentity();

		transform.setFromOpenGLMatrix(&finalTransformMat[0][0]);
		m_RigidBody->setWorldTransform(transform);

		if (m_RigidBody->getCollisionShape())
		{
			m_RigidBody->getCollisionShape()->setLocalScaling(ToBtVec3(m_LocalScale * m_ParentTransform->GetWorldScale()));
		}

		m_RigidBody->activate();
	}

	void RigidBody::GetUpRightForward(btVector3& up, btVector3& right, btVector3& forward)
	{
		const btTransform& transform = m_RigidBody->getWorldTransform();
		btQuaternion rot = transform.getRotation();
		btMatrix3x3 rotMat = btMatrix3x3(rot).transpose();
		right = rotMat[0];
		up = rotMat[1];
		forward = rotMat[2];
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
} // namespace flex
