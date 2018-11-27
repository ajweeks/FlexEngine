#pragma once

#include "JSONTypes.hpp"

namespace flex
{
	class RigidBody;
	class GameObject;

	class Transform
	{
	public:
		Transform();
		Transform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);
		Transform(const glm::vec3& position, const glm::quat& rotation);
		Transform(const glm::vec3& position);

		Transform(const Transform& other);
		Transform(const Transform&& other);
		Transform& operator=(const Transform& other);
		Transform& operator=(const Transform&& other);

		~Transform();

		static Transform ParseJSON(const JSONObject& object);
		JSONField SerializeToJSON();

		void SetAsIdentity();

		glm::vec3 GetLocalPosition() const;
		glm::vec3 GetWorldPosition() const;

		glm::quat GetLocalRotation() const;
		glm::quat GetWorldRotation() const;

		glm::vec3 GetLocalScale() const;
		glm::vec3 GetWorldScale() const;

		glm::vec3 GetRight() const;
		glm::vec3 GetUp() const;
		glm::vec3 GetForward() const;

		void SetLocalPosition(const glm::vec3& position, bool bUpdateChain = true);
		void SetWorldPosition(const glm::vec3& position, bool bUpdateChain = true);

		void SetLocalRotation(const glm::quat& quatRotation, bool bUpdateChain = true);
		void SetWorldRotation(const glm::quat& quatRotation, bool bUpdateChain = true);

		void SetLocalScale(const glm::vec3& scale, bool bUpdateChain = true);
		void SetWorldScale(const glm::vec3& scale, bool bUpdateChain = true);

		void SetWorldFromMatrix(const glm::mat4& mat, bool bUpdateChain = true);

		void Translate(const glm::vec3& deltaPosition);
		void Translate(real deltaX, real deltaY, real deltaZ);

		void Rotate(const glm::quat& deltaQuatRotation);

		void Scale(const glm::vec3& deltaScale);
		void Scale(real deltaScale);
		void Scale(real deltaX, real deltaY, real deltaZ);

		void SetWorldTransform(const glm::mat4& desiredWorldTransform);

		bool IsIdentity() const;
		static Transform Identity();

		void SetGameObject(GameObject* gameObject);
		GameObject* GetGameObject() const;

		const glm::mat4& GetWorldTransform() const;
		const glm::mat4& GetLocalTransform() const;

		void UpdateParentTransform(); // Climbs up the parent-child tree up to the root

	private:
		void UpdateChildTransforms(); // Climbs down the parent-child trees to all leaves

		glm::mat4 localTransform;
		glm::mat4 worldTransform;

		glm::vec3 localPosition;
		glm::quat localRotation;
		glm::vec3 localScale;

		glm::vec3 worldPosition;
		glm::quat worldRotation;
		glm::vec3 worldScale;

		glm::vec3 forward;
		glm::vec3 up;
		glm::vec3 right;

		GameObject* m_GameObject = nullptr;

		static Transform m_Identity;

	};
} // namespace flex
