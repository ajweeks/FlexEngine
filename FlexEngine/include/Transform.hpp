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
		
		void SetAsIdentity();

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

		glm::vec3 localPosition;
		glm::quat localRotation;
		glm::vec3 localScale;

		glm::vec3 globalPosition;
		glm::quat globalRotation;
		glm::vec3 globalScale;

		Transform* parentTransform = nullptr;
		std::vector<Transform*> childrenTransforms;

	private:
		void UpdateParentTransform(); // Used to go all the way to the base of the parent-child tree
		void UpdateChildTransforms(); // Used to go back down to the lowest node of the parent-child tree

	};
} // namespace flex
