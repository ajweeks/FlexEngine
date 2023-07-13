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

		JSONObject Serialize() const;
		static JSONObject Serialize(const glm::mat4 matrix, const char* objName);
		static JSONObject Serialize(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale, const char* objName);

		void SetAsIdentity();

		bool IsIdentity() const;

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

		glm::mat4 GetWorldTransform();

		void MarkDirty(bool bPos, bool bRot, bool bScale);

		void UpdateRigidBodyRecursive();

		static void Decompose(const glm::mat4& mat, glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale);

		static const Transform Identity;
		static const float DirtyThreshold;

	private:
		friend struct MotionState;

		void SetLocalPositionInternal(const glm::vec3& newLocalPos);
		void SetLocalRotationInternal(const glm::quat& newLocalRot);
		void SetLocalScaleInternal(const glm::vec3& newLocalScale);

		void Recompute();
		void ClearDirtyFlags();
		void UpdateRigidBody();

		// Callback from physics system
		void OnRigidbodyTransformChanged(const glm::vec3& position, const glm::quat& rotation);

		// Only valid when bDirty is false
		glm::mat4 cachedWorldTransform;

		glm::quat localRotation;
		glm::vec3 localPosition;
		glm::vec3 localScale;

		GameObject* m_GameObject = nullptr;

		bool bDirtyPos : 1;
		bool bDirtyRot : 1;
		bool bDirtyScale : 1;

	};
} // namespace flex
