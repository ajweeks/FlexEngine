#include "stdafx.hpp"

#include "Transform.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

namespace flex
{
	Transform::Transform()
	{
		SetAsIdentity();
	}

	Transform::Transform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) :
		localPosition(position),
		localRotation(rotation),
		localScale(scale)
	{
	}

	Transform::Transform(const glm::vec3& position, const glm::quat& rotation) :
		localPosition(position),
		localRotation(rotation),
		localScale(glm::vec3(1.0f))
	{
	}

	Transform::Transform(const glm::vec3& position) :
		localPosition(position),
		localRotation(glm::quat(glm::vec3(0.0f))),
		localScale(glm::vec3(1.0f))
	{
	}

	Transform::~Transform()
	{
	}

	void Transform::Translate(glm::vec3 deltaPosition)
	{
		localPosition += deltaPosition;

		UpdateParentTransform();
	}

	void Transform::Translate(float deltaX, float deltaY, float deltaZ)
	{
		localPosition.x += deltaX;
		localPosition.y += deltaY;
		localPosition.z += deltaZ;

		UpdateParentTransform();
	}

	void Transform::Rotate(glm::quat deltaQuatRotation)
	{
		localRotation *= deltaQuatRotation;

		UpdateParentTransform();
	}

	void Transform::Rotate(glm::vec3 deltaEulerRotationRad)
	{
		glm::quat rotationQuat(deltaEulerRotationRad);
		localRotation *= rotationQuat;

		UpdateParentTransform();
	}

	void Transform::Rotate(float deltaX, float deltaY, float deltaZ)
	{
		glm::quat rotationQuat(glm::vec3(deltaX, deltaY, deltaZ));
		localRotation *= rotationQuat;

		UpdateParentTransform();
	}

	void Transform::Scale(glm::vec3 deltaScale)
	{
		localScale *= deltaScale;

		UpdateParentTransform();
	}

	void Transform::Scale(float deltaX, float deltaY, float deltaZ)
	{
		localScale.x *= deltaX;
		localScale.y *= deltaY;
		localScale.z *= deltaZ;

		UpdateParentTransform();
	}

	void Transform::SetParentTransform(Transform* parent)
	{
		parentTransform = parent;
	}

	void Transform::AddChildTransform(Transform* child)
	{
		childrenTransforms.push_back(child);

		UpdateParentTransform();
	}

	void Transform::RemoveChildTransform(Transform* child)
	{
		for (auto iter = childrenTransforms.begin(); iter != childrenTransforms.end(); ++iter)
		{
			if (*iter == child)
			{
				childrenTransforms.erase(iter);
				return;
			}
		}
	}

	void Transform::RemoveAllChildTransforms()
	{
		auto iter = childrenTransforms.begin();
		while (iter != childrenTransforms.end())
		{
			iter = childrenTransforms.erase(iter);
		}
	}

	void Transform::SetAsIdentity()
	{
		localPosition = glm::vec3(0.0f);
		localRotation = glm::quat(glm::vec3(0.0f));
		localScale = glm::vec3(1.0f);

		UpdateParentTransform();
	}

	glm::mat4 Transform::GetModelMatrix()
	{
		glm::mat4 matTrans = glm::translate(glm::mat4(1.0f), globalPosition);
		glm::mat4 matRot = glm::mat4(globalRotation);
		glm::mat4 matScale = glm::scale(glm::mat4(1.0f), globalScale);
		glm::mat4 matModel = matTrans * matRot * matScale;

		return matModel;
	}

	Transform Transform::Identity()
	{
		Transform result;

		result.SetAsIdentity();

		return result;
	}

	void Transform::UpdateParentTransform()
	{
		if (parentTransform)
		{
			parentTransform->UpdateParentTransform();
		}
		else
		{
			globalPosition = localPosition;
			globalRotation = localRotation;
			globalScale = localScale;

			UpdateChildTransforms();
		}
	}

	void Transform::UpdateChildTransforms()
	{
		for (auto iter = childrenTransforms.begin(); iter != childrenTransforms.end(); ++iter)
		{
			Transform* childTransform = *iter;

			childTransform->globalPosition = childTransform->localPosition + localPosition;
			childTransform->globalScale = childTransform->localScale * localScale;
			childTransform->globalRotation = childTransform->localRotation * localRotation;

			childTransform->UpdateChildTransforms();
		}
	}

} // namespace flex
