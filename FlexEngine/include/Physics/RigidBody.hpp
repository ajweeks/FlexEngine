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
		RigidBody(int group = 1, int mask = 1);
		virtual ~RigidBody();

		void Initialize(btCollisionShape* collisionShape, const GameContext& gameContext, btTransform& startingTransform, bool isKinematic = false, bool isStatic = false);
		void Destroy(const GameContext& gameContext);

		void SetMass(real mass);

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

		int m_Group = 0;
		int m_Mask = 0;

		real m_Mass = 0.0f;

	};
} // namespace flex
