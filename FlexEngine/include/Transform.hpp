#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>

namespace flex
{
	struct Transform
	{
		Transform();
		Transform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);
		Transform(const glm::vec3& position, const glm::quat& rotation);
		Transform(const glm::vec3& position);
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

		void SetLocalPosition(glm::vec3 position);
		void SetGlobalPosition(glm::vec3 position);

		void SetLocalRotation(glm::quat quatRotation);
		void SetGlobalRotation(glm::quat quatRotation);
		void SetLocalRotation(glm::vec3 eulerAnglesRad);
		void SetGlobalRotation(glm::vec3 eulerAnglesRad);
		void SetLocalRotation(float eulerXRad, float eulerYRad, float eulerZRad);
		void SetGlobalRotation(float eulerXRad, float eulerYRad, float eulerZRad);

		void SetLocalScale(glm::vec3 scale);
		void SetGlobalScale(glm::vec3 scale);


		void Translate(glm::vec3 deltaPosition);
		void Translate(float deltaX, float deltaY, float deltaZ);

		void Rotate(glm::quat deltaQuatRotation);
		void Rotate(glm::vec3 deltaEulerRotationRad);
		void Rotate(float deltaX, float deltaY, float deltaZ);
		
		void Scale(glm::vec3 deltaScale);
		void Scale(float deltaX, float deltaY, float deltaZ);

		void SetParentTransform(Transform* parent);
		void AddChildTransform(Transform* child);
		void RemoveChildTransform(Transform* child);
		void RemoveAllChildTransforms();
		
		glm::mat4 GetModelMatrix();

		static Transform Identity();

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

	};
} // namespace flex
