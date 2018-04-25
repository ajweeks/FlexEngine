#include "stdafx.hpp"

#include "Transform.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "BulletDynamics/Dynamics/btRigidBody.h"

#include "Physics/RigidBody.hpp"

namespace flex
{
	Transform Transform::m_Identity = Transform(glm::vec3(0.0f), glm::quat(glm::vec3(0.0f)), glm::vec3(1.0f));

	Transform::Transform()
	{
		SetAsIdentity();
	}

	Transform::Transform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) :
		localPosition(position),
		localRotation(rotation),
		localScale(scale),
		globalPosition(localPosition),
		globalRotation(localRotation),
		globalScale(localScale)
	{
	}

	Transform::Transform(const glm::vec3& position, const glm::quat& rotation) :
		localPosition(position),
		localRotation(rotation),
		localScale(glm::vec3(1.0f)),
		globalPosition(localPosition),
		globalRotation(localRotation),
		globalScale(localScale)
	{
	}

	Transform::Transform(const glm::vec3& position) :
		localPosition(position),
		localRotation(glm::quat(glm::vec3(0.0f))),
		localScale(glm::vec3(1.0f)),
		globalPosition(localPosition),
		globalRotation(localRotation),
		globalScale(localScale)
	{
	}

	Transform::Transform(const Transform& other) :
		localPosition(other.localPosition),
		localRotation(other.localRotation),
		localScale(other.localScale),
		globalPosition(other.globalPosition),
		globalRotation(other.globalRotation),
		globalScale(other.globalScale)
	{
		// Don't copy parent/children
	}

	Transform::Transform(const Transform&& other) :
		localPosition(std::move(other.localPosition)),
		localRotation(std::move(other.localRotation)),
		localScale(std::move(other.localScale)),
		globalPosition(std::move(other.globalPosition)),
		globalRotation(std::move(other.globalRotation)),
		globalScale(std::move(other.globalScale))
	{
		// Don't copy parent/children
	}

	Transform& Transform::operator=(const Transform& other)
	{
		localPosition = other.localPosition;
		localRotation = other.localRotation;
		localScale = other.localScale;
		globalPosition = other.globalPosition;
		globalRotation = other.globalRotation;
		globalScale = other.globalScale;
		// Don't copy parent/ children

		return *this;
	}

	Transform& Transform::operator=(const Transform&& other)
	{
		if (this != &other)
		{
			localPosition = std::move(other.localPosition);
			localRotation = std::move(other.localRotation);
			localScale = std::move(other.localScale);
			globalPosition = std::move(other.globalPosition);
			globalRotation = std::move(other.globalRotation);
			globalScale = std::move(other.globalScale);
			parentTransform = other.parentTransform;
			childrenTransforms = std::move(other.childrenTransforms);
		}

		return *this;
	}

	Transform::~Transform()
	{
	}

	void Transform::Translate(const glm::vec3& deltaPosition)
	{
		localPosition += deltaPosition;

		UpdateParentTransform();
	}

	void Transform::Translate(real deltaX, real deltaY, real deltaZ)
	{
		localPosition.x += deltaX;
		localPosition.y += deltaY;
		localPosition.z += deltaZ;

		UpdateParentTransform();
	}

	void Transform::Rotate(const glm::quat& deltaQuatRotation)
	{
		localRotation *= deltaQuatRotation;

		UpdateParentTransform();
	}

	void Transform::Rotate(const glm::vec3& deltaEulerRotationRad)
	{
		glm::quat rotationQuat(deltaEulerRotationRad);
		localRotation *= rotationQuat;

		UpdateParentTransform();
	}

	void Transform::Rotate(real deltaX, real deltaY, real deltaZ)
	{
		glm::quat rotationQuat(glm::vec3(deltaX, deltaY, deltaZ));
		localRotation *= rotationQuat;

		UpdateParentTransform();
	}

	void Transform::Scale(const glm::vec3& deltaScale)
	{
		localScale *= deltaScale;

		UpdateParentTransform();
	}

	void Transform::Scale(real deltaScale)
	{
		Scale(glm::vec3(deltaScale));
	}

	void Transform::Scale(real deltaX, real deltaY, real deltaZ)
	{
		Scale(glm::vec3(deltaX, deltaY, deltaZ));
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
			(*iter)->RemoveAllChildTransforms();
			iter = childrenTransforms.erase(iter);
		}
	}

	void Transform::Update()
	{
		UpdateParentTransform();
	}

	void Transform::SetAsIdentity()
	{
		*this = m_Identity;
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

	bool Transform::IsIdentity() const
	{
		bool identity = (localPosition == m_Identity.localPosition &&
						localRotation == m_Identity.localRotation &&
						localScale == m_Identity.localScale);
		return identity;
	}

	Transform Transform::Identity()
	{
		Transform result;

		result.SetAsIdentity();

		return result;
	}

	Transform* Transform::GetParent()
	{
		return parentTransform;
	}

	const std::vector<Transform*>& Transform::GetChildren()
	{
		return childrenTransforms;
	}

	void Transform::SetOwnerRenderID(RenderID ownerRenderID)
	{
		m_OwnerRenderID = ownerRenderID;
	}

	RenderID Transform::GetOwnerRenderID() const
	{
		return m_OwnerRenderID;
	}

	void Transform::UpdateParentTransform()
	{
		if (parentTransform)
		{
			parentTransform->UpdateParentTransform();
		}
		else
		{
			UpdateChildTransforms();
		}
	}

	void Transform::UpdateChildTransforms()
	{
		if (parentTransform)
		{
			globalPosition = parentTransform->globalPosition + localPosition;
			globalScale = parentTransform->globalScale * localScale;
			globalRotation = parentTransform->globalRotation * localRotation;
		}
		else
		{
			globalPosition = localPosition;
			globalRotation = localRotation;
			globalScale = localScale;
		}

		for (auto iter = childrenTransforms.begin(); iter != childrenTransforms.end(); ++iter)
		{
			(*iter)->UpdateChildTransforms();
		}
	}

	glm::vec3 Transform::GetLocalPosition()
	{
		return localPosition;
	}

	glm::vec3 Transform::GetGlobalPosition()
	{
		return globalPosition;
	}

	glm::quat Transform::GetLocalRotation()
	{
		return localRotation;
	}

	glm::quat Transform::GetGlobalRotation()
	{
		return globalRotation;
	}

	glm::vec3 Transform::GetLocalScale()
	{
		return localScale;
	}

	glm::vec3 Transform::GetGlobalScale()
	{
		return globalScale;
	}

	void Transform::SetLocalPosition(const glm::vec3& position)
	{
		localPosition = position;

		UpdateParentTransform();
	}

	void Transform::SetGlobalPosition(const glm::vec3& position)
	{
		if (parentTransform)
		{
			localPosition = position- parentTransform->GetGlobalPosition();
		}
		else
		{
			localPosition = position;
			// Global position will be set in next function call
		}
		
		UpdateParentTransform();
	}

	void Transform::SetLocalRotation(const glm::quat& quatRotation)
	{
		localRotation = quatRotation;

		UpdateParentTransform();
	}

	void Transform::SetGlobalRotation(const glm::quat& quatRotation)
	{
		if (parentTransform)
		{
			localRotation = quatRotation + -parentTransform->GetGlobalRotation();
		}
		else
		{
			localRotation = quatRotation;
			// Global rotation will be set in next function call
		}

		UpdateParentTransform();
	}

	void Transform::SetLocalRotation(const glm::vec3& eulerAnglesRad)
	{
		localRotation = glm::quat(eulerAnglesRad);

		UpdateParentTransform();
	}

	void Transform::SetGlobalRotation(const glm::vec3& eulerAnglesRad)
	{
		if (parentTransform)
		{
			localRotation = glm::quat(eulerAnglesRad) + -parentTransform->GetGlobalRotation();
		}
		else
		{
			localRotation = glm::quat(eulerAnglesRad);
			// Global rotation will be set in next function call
		}

		UpdateParentTransform();
	}

	void Transform::SetLocalRotation(real eulerXRad, real eulerYRad, real eulerZRad)
	{
		localRotation = glm::quat(glm::vec3(eulerXRad, eulerYRad, eulerZRad));

		UpdateParentTransform();
	}

	void Transform::SetGlobalRotation(real eulerXRad, real eulerYRad, real eulerZRad)
	{
		if (parentTransform)
		{
			localRotation = glm::quat(glm::vec3(eulerXRad, eulerYRad, eulerZRad)) + -parentTransform->GetGlobalRotation();
		}
		else
		{
			localRotation = glm::quat(glm::vec3(eulerXRad, eulerYRad, eulerZRad));
			// Global rotation will be set in next function call
		}

		UpdateParentTransform();
	}

	void Transform::SetLocalScale(const glm::vec3& scale)
	{
		localScale = scale;

		UpdateParentTransform();
	}

	void Transform::SetGlobalScale(const glm::vec3& scale)
	{
		if (parentTransform)
		{
			localScale = scale / parentTransform->GetGlobalScale();
		}
		else
		{
			localScale = scale;
			// Global scale will be set in next function call
		}

		UpdateParentTransform();
	}

	void Transform::MatchRigidBody(RigidBody* rigidBody, bool forceUpdate)
	{
		btRigidBody* rb = rigidBody->GetRigidBodyInternal();
		bool update = forceUpdate || (!rb->isStaticObject() && rb->isActive());
		if (update)
		{
			glm::vec3 pos;
			glm::quat rot;
			rigidBody->GetTransform(pos, rot);

			SetGlobalPosition(pos);
			SetGlobalRotation(rot);
		}
	}

} // namespace flex
