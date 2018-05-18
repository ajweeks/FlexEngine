#pragma once

#include "Physics/PhysicsHelpers.hpp"

class btRigidBody;
class btCollisionShape;
class btMotionState;
class btTransform;

namespace flex
{
	class RigidBody
	{
	public:
		RigidBody(i32 group = 1, i32 mask = 1);
		virtual ~RigidBody();

		void Initialize(btCollisionShape* collisionShape, const GameContext& gameContext, btTransform& startingTransform);
		void Destroy(const GameContext& gameContext);

		void SetMass(real mass);
		real GetMass() const;
		void SetKinematic(bool bKinematic);
		bool IsKinematic() const;
		void SetStatic(bool bStatic);
		bool IsStatic() const;
		void SetFriction(real friction);
		real GetFriction() const;

		void GetTransform(glm::vec3& outPos, glm::quat& outRot);
		//glm::vec3 GetPosition();
		//glm::quat GetRotation();
		
		void SetPosition(const glm::vec3& pos);
		void SetRotation(const glm::quat& rot);
		void SetScale(const glm::vec3& scale);

		void GetUpRightForward(btVector3& up, btVector3& right, btVector3& forward);

		btRigidBody* GetRigidBodyInternal();

		void SetPhysicsFlags(u32 flags);
		u32 GetPhysicsFlags();

	private:
		btRigidBody* m_RigidBody = nullptr;
		btMotionState* m_MotionState = nullptr;

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
