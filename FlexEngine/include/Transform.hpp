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
		explicit Transform(const glm::vec3& position);

		Transform(const Transform& other);
		Transform(const Transform&& other);
		Transform& operator=(const Transform& other) = delete;
		Transform& operator=(const Transform&& other) = delete;

		~Transform() = default;

		static void ParseJSON(const JSONObject& object, Transform& outTransform);
		static void ParseJSON(const JSONObject& object, glm::mat4& outModel);
		static void ParseJSON(const JSONObject& object, glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale);

		// Copies all fields (except game object reference) into this transform
		void CloneFrom(const Transform& other);

		JSONField Serialize() const;
		static JSONField Serialize(const glm::mat4 matrix, const char* objName);
		static JSONField Serialize(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale, const char* objName);

		void SetAsIdentity();

		bool IsIdentity() const;

		glm::vec3 GetLocalPosition() const;
		glm::vec3 GetWorldPosition();

		glm::quat GetLocalRotation() const;
		glm::quat GetWorldRotation();

		glm::vec3 GetLocalScale() const;
		glm::vec3 GetWorldScale();

		glm::vec3 GetRight();
		glm::vec3 GetUp();
		glm::vec3 GetForward();

		void SetLocalPosition(const glm::vec3& position, bool bUpdateChain = true);
		void SetWorldPosition(const glm::vec3& position, bool bUpdateChain = true);

		void SetLocalRotation(const glm::quat& rotation, bool bUpdateChain = true);
		void SetWorldRotation(const glm::quat& rotation, bool bUpdateChain = true);

		void SetLocalScale(const glm::vec3& scale, bool bUpdateChain = true);
		void SetWorldScale(const glm::vec3& scale, bool bUpdateChain = true);

		void SetWorldFromMatrix(const glm::mat4& mat, bool bUpdateChain = true);

		void Translate(const glm::vec3& deltaPosition);
		void Translate(real deltaX, real deltaY, real deltaZ);

		void Rotate(const glm::quat& deltaRotation);

		void Scale(const glm::vec3& deltaScale);
		void Scale(real deltaScale);
		void Scale(real deltaX, real deltaY, real deltaZ);

		void SetWorldTransform(const glm::mat4& desiredWorldTransform);
		void SetFromBtTransform(const btTransform& transform);

		void SetGameObject(GameObject* gameObject);
		GameObject* GetGameObject() const;

		const glm::mat4& GetLocalTransform() const;
		const glm::mat4& GetWorldTransform();

		void ComputeValues(); // Climbs up the parent-child tree up to the root
		void MarkDirty();

		static const Transform Identity;

		bool updateParentOnStateChange = false;

	private:
		friend struct MotionState;

		// Callback from physics system
		void OnRigidbodyTransformChanged(const glm::vec3& position, const glm::quat& rotation);

		bool bDirty = true;

		glm::vec3 localPosition;
		glm::quat localRotation;
		glm::vec3 localScale;

		glm::vec3 worldPosition;
		glm::quat worldRotation;
		glm::vec3 worldScale;

		glm::mat4 localTransform;
		glm::mat4 worldTransform;

		glm::vec3 forward;
		glm::vec3 up;
		glm::vec3 right;

		GameObject* m_GameObject = nullptr;

	};
} // namespace flex
