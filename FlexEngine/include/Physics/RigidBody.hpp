#pragma once

class btRigidBody;
class btCollisionShape;
class btMotionState;

namespace flex
{
	class RigidBody
	{
	public:
		RigidBody();
		virtual ~RigidBody();

		void Initialize(btCollisionShape* collisionShape, const GameContext& gameContext, bool isKinematic = false, bool isStatic = false);
		void Destroy(const GameContext& gameContext);

		void SetMass(real mass);

		void GetTransform(glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale);
		//glm::vec3 GetPosition();
		//glm::quat GetRotation();
		
		void SetPosition(const glm::vec3& pos);
		void SetRotation(const glm::quat& rot);
		void SetScale(const glm::vec3& scale);

	private:
		btRigidBody* m_RigidBody = nullptr;
		btMotionState* m_MotionState = nullptr;

		real m_Mass = 0.0f;

	};
} // namespace flex
