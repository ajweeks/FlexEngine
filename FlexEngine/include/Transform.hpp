#pragma once

#pragma warning(push, 0)
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#pragma warning(pop)

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
		
		// Call when parent-child trees need to be updated
		void Update(); 

		void SetAsIdentity();

		glm::vec3 GetLocalPosition() const;
		glm::vec3 GetWorldlPosition() const;

		glm::quat GetLocalRotation() const;
		glm::quat GetWorldlRotation() const;

		glm::vec3 GetLocalScale() const;
		glm::vec3 GetWorldlScale() const;

		void SetLocalPosition(const glm::vec3& position);
		void SetWorldlPosition(const glm::vec3& position);

		void SetLocalRotation(const glm::quat& quatRotation);
		void SetWorldRotation(const glm::quat& quatRotation);
		void SetLocalRotation(const glm::vec3& eulerAnglesRad);
		void SetWorldRotation(const glm::vec3& eulerAnglesRad);
		void SetLocalRotation(real eulerXRad, real eulerYRad, real eulerZRad);
		void SetWorldRotation(real eulerXRad, real eulerYRad, real eulerZRad);

		void SetLocalScale(const glm::vec3& scale);
		void SetWorldScale(const glm::vec3& scale);

		void MatchRigidBody(RigidBody* rigidBody, bool forceUpdate = false);


		void Translate(const glm::vec3& deltaPosition);
		void Translate(real deltaX, real deltaY, real deltaZ);

		void Rotate(const glm::quat& deltaQuatRotation);
		void Rotate(const glm::vec3& deltaEulerRotationRad);
		void Rotate(real deltaX, real deltaY, real deltaZ);
		
		void Scale(const glm::vec3& deltaScale);
		void Scale(real deltaScale);
		void Scale(real deltaX, real deltaY, real deltaZ);

		bool IsIdentity() const;
		static Transform Identity();

		void SetGameObject(GameObject* gameObject);
		GameObject* GetGameObject() const;

		glm::mat4 GetWorldTransform();
		glm::mat4 GetLocalTransform();

	private:
		void UpdateParentTransform(); // Used to go all the way to the base of the parent-child tree
		void UpdateChildTransforms(); // Used to go back down to the lowest node of the parent-child tree

		glm::mat4 localTransform;
		glm::mat4 worldTransform;

		glm::vec3 localPosition;
		glm::quat localRotation;
		glm::vec3 localScale;

		glm::vec3 worldPosition;
		glm::quat worldRotation;
		glm::vec3 worldScale;

		GameObject* m_GameObject = nullptr;

		static Transform m_Identity;

	};
} // namespace flex
