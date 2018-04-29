#pragma once

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
		bool GetKinematic() const;
		void SetStatic(bool bStatic);
		bool GetStatic() const;

		void GetTransform(glm::vec3& outPos, glm::quat& outRot);
		//glm::vec3 GetPosition();
		//glm::quat GetRotation();
		
		void SetPosition(const glm::vec3& pos);
		void SetRotation(const glm::quat& rot);
		void SetScale(const glm::vec3& scale);

		btRigidBody* GetRigidBodyInternal();

	private:
		btRigidBody* m_RigidBody = nullptr;
		btMotionState* m_MotionState = nullptr;

		bool m_bStatic = false;
		bool m_bKinematic = false;

		i32 m_Group = 0;
		i32 m_Mask = 0;

		real m_Mass = 0.0f;

	};
} // namespace flex
