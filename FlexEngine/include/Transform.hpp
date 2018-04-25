#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>

namespace flex
{
	class RigidBody;
	
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

		glm::vec3 GetLocalPosition();
		glm::vec3 GetGlobalPosition();

		glm::quat GetLocalRotation();
		glm::quat GetGlobalRotation();

		glm::vec3 GetLocalScale();
		glm::vec3 GetGlobalScale();

		void SetLocalPosition(const glm::vec3& position);
		void SetGlobalPosition(const glm::vec3& position);

		void SetLocalRotation(const glm::quat& quatRotation);
		void SetGlobalRotation(const glm::quat& quatRotation);
		void SetLocalRotation(const glm::vec3& eulerAnglesRad);
		void SetGlobalRotation(const glm::vec3& eulerAnglesRad);
		void SetLocalRotation(real eulerXRad, real eulerYRad, real eulerZRad);
		void SetGlobalRotation(real eulerXRad, real eulerYRad, real eulerZRad);

		void SetLocalScale(const glm::vec3& scale);
		void SetGlobalScale(const glm::vec3& scale);

		void MatchRigidBody(RigidBody* rigidBody, bool forceUpdate = false);


		void Translate(const glm::vec3& deltaPosition);
		void Translate(real deltaX, real deltaY, real deltaZ);

		void Rotate(const glm::quat& deltaQuatRotation);
		void Rotate(const glm::vec3& deltaEulerRotationRad);
		void Rotate(real deltaX, real deltaY, real deltaZ);
		
		void Scale(const glm::vec3& deltaScale);
		void Scale(real deltaScale);
		void Scale(real deltaX, real deltaY, real deltaZ);

		void SetParentTransform(Transform* parent);
		void AddChildTransform(Transform* child);
		void RemoveChildTransform(Transform* child);
		void RemoveAllChildTransforms();
		
		glm::mat4 GetModelMatrix();

		bool IsIdentity() const;

		static Transform Identity();

		Transform* GetParent();
		const std::vector<Transform*>& GetChildren();

		void SetOwnerRenderID(RenderID ownerRenderID);
		RenderID GetOwnerRenderID() const;

	private:
		void UpdateParentTransform(); // Used to go all the way to the base of the parent-child tree
		void UpdateChildTransforms(); // Used to go back down to the lowest node of the parent-child tree

		glm::vec3 localPosition;
		glm::quat localRotation;
		glm::vec3 localScale;

		glm::vec3 globalPosition;
		glm::quat globalRotation;
		glm::vec3 globalScale;

		Transform* parentTransform = nullptr;
		std::vector<Transform*> childrenTransforms;

		RenderID m_OwnerRenderID = InvalidRenderID;

		static Transform m_Identity;

	};
} // namespace flex
