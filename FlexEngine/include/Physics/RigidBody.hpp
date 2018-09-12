#pragma once

#include "Physics/PhysicsHelpers.hpp"

class btRigidBody;
class btCollisionShape;
class btMotionState;
class btTransform;
class btTypedConstraint;

namespace flex
{
	class RigidBody
	{
	public:
		RigidBody(i32 group = -1, i32 mask = -1);
		// NOTE: This copy constructor does not initialize data, it only copies POD fields
		RigidBody(const RigidBody& other);
		virtual ~RigidBody();

		void Initialize(btCollisionShape* collisionShape, Transform* parentTransform);
		void Destroy();

		void AddConstraint(btTypedConstraint* constraint);

		void SetMass(real mass);
		real GetMass() const;
		void SetKinematic(bool bKinematic);
		bool IsKinematic() const;
		void SetStatic(bool bStatic);
		bool IsStatic() const;
		void SetFriction(real friction);
		real GetFriction() const;

		void GetTransform(glm::vec3& outPos, glm::quat& outRot);
		
		// Set local transform (relative to parent transform)
		void SetLocalSRT(const glm::vec3& scale, const glm::quat& rot, const glm::vec3& pos);
		void SetLocalPosition(const glm::vec3& pos);
		void SetLocalRotation(const glm::quat& rot);
		void SetLocalScale(const glm::vec3& scale);

		i32 GetGroup() const;
		// NOTE: This function removes, then re-adds this object to the world!
		void SetGroup(i32 group);

		i32 GetMask() const;
		// NOTE: This function removes, then re-adds this object to the world!
		void SetMask(i32 mask);

		glm::vec3 GetLocalPosition() const;
		glm::quat GetLocalRotation() const;
		glm::vec3 GetLocalScale() const;

		// Applies physics-driven transform to parent transform (taking into account local transform)
		void UpdateParentTransform();

		// Sets physics-driven transform to parent transform (taking into account local transform)
		void MatchParentTransform();

		void GetUpRightForward(btVector3& up, btVector3& right, btVector3& forward);

		btRigidBody* GetRigidBodyInternal();

		void SetPhysicsFlags(u32 flags);
		u32 GetPhysicsFlags();

	private:
		btRigidBody* m_RigidBody = nullptr;
		btMotionState* m_MotionState = nullptr;

		std::vector<btTypedConstraint*> m_Constraints;

		Transform* m_ParentTransform = nullptr;
		glm::vec3 m_LocalPosition;
		glm::quat m_LocalRotation;
		glm::vec3 m_LocalScale;

		// Must be 0 if static, non-zero otherwise
		real m_Mass = 1.0f;
		bool m_bStatic = false;
		bool m_bKinematic = false;
		real m_Friction = 1.0f;

		i32 m_Group = 0;
		i32 m_Mask = 0;

		// Flags set from PhysicsFlag enum
		u32 m_Flags = 0;
	};
} // namespace flex
