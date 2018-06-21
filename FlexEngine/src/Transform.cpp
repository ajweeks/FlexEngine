#include "stdafx.hpp"

#include "Transform.hpp"

#pragma warning(push, 0)
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "BulletDynamics/Dynamics/btRigidBody.h"
#pragma warning(pop)

#include "Physics/RigidBody.hpp"
#include "Scene/GameObject.hpp"

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
		worldPosition(position),
		worldRotation(rotation),
		worldScale(scale),
		localTransform(glm::translate(glm::mat4(1.0f), position) *
					   // Cast away constness
					   glm::mat4((glm::quat)rotation) *
					   glm::scale(glm::mat4(1.0f), scale)),
		worldTransform(glm::translate(glm::mat4(1.0f), position) *
					   glm::mat4((glm::quat)rotation) *
					   glm::scale(glm::mat4(1.0f), scale))
	{
	}

	Transform::Transform(const glm::vec3& position, const glm::quat& rotation) :
		localPosition(position),
		localRotation(rotation),
		localScale(glm::vec3(1.0f)),
		worldPosition(position),
		worldRotation(rotation),
		worldScale(glm::vec3(1.0f)),
		localTransform(glm::translate(glm::mat4(1.0f), position) *
					   glm::mat4((glm::quat)rotation) *
					   glm::scale(glm::mat4(1.0f), glm::vec3(1.0f))),
		worldTransform(glm::translate(glm::mat4(1.0f), position) *
					   glm::mat4((glm::quat)rotation) *
					   glm::scale(glm::mat4(1.0f), glm::vec3(1.0f)))
	{
	}

	Transform::Transform(const glm::vec3& position) :
		localPosition(position),
		localRotation(glm::quat(glm::vec3(0.0f))),
		localScale(glm::vec3(1.0f)),
		worldPosition(position),
		worldRotation(glm::quat(glm::vec3(0.0f))),
		worldScale(glm::vec3(1.0f)),
		localTransform(glm::translate(glm::mat4(1.0f), position) *
					   glm::mat4(glm::quat(glm::vec3(0.0f))) *
					   glm::scale(glm::mat4(1.0f), glm::vec3(1.0f))),
		worldTransform(glm::translate(glm::mat4(1.0f), position) *
					   glm::mat4(glm::quat(glm::vec3(0.0f))) *
					   glm::scale(glm::mat4(1.0f), glm::vec3(1.0f)))
	{
	}

	Transform::Transform(const Transform& other) :
		localPosition(other.localPosition),
		localRotation(other.localRotation),
		localScale(other.localScale),
		worldPosition(other.worldPosition),
		worldRotation(other.worldRotation),
		worldScale(other.worldScale)
	{
		localTransform = (glm::translate(glm::mat4(1.0f), other.localPosition) *
						  glm::mat4((glm::quat)other.localRotation) *
						  glm::scale(glm::mat4(1.0f), other.localScale));
		worldTransform = (glm::translate(glm::mat4(1.0f), other.worldPosition) *
					      glm::mat4((glm::quat)other.worldRotation) *
					      glm::scale(glm::mat4(1.0f), other.worldScale));
	}

	Transform::Transform(const Transform&& other) :
		localPosition(std::move(other.localPosition)),
		localRotation(std::move(other.localRotation)),
		localScale(std::move(other.localScale)),
		worldPosition(std::move(other.worldPosition)),
		worldRotation(std::move(other.worldRotation)),
		worldScale(std::move(other.worldScale))
	{
		localTransform = (glm::translate(glm::mat4(1.0f), localPosition) *
						  glm::mat4(localRotation) *
						  glm::scale(glm::mat4(1.0f), localScale));
		worldTransform = (glm::translate(glm::mat4(1.0f), worldPosition) *
					      glm::mat4(worldRotation) *
					      glm::scale(glm::mat4(1.0f), worldScale));
	}

	Transform& Transform::operator=(const Transform& other)
	{
		localPosition = other.localPosition;
		localRotation = other.localRotation;
		localScale = other.localScale;
		worldPosition = other.worldPosition;
		worldRotation = other.worldRotation;
		worldScale = other.worldScale;
		localTransform = glm::mat4(glm::translate(glm::mat4(1.0f), localPosition) *
								   glm::mat4(localRotation) *
								   glm::scale(glm::mat4(1.0f), localScale));
		worldTransform = glm::mat4(glm::translate(glm::mat4(1.0f), worldPosition) *
								   glm::mat4(worldRotation) *
								   glm::scale(glm::mat4(1.0f), worldScale));
			return *this;
	}

	Transform& Transform::operator=(const Transform&& other)
	{
		if (this != &other)
		{
			localPosition = std::move(other.localPosition);
			localRotation = std::move(other.localRotation);
			localScale = std::move(other.localScale);
			worldPosition = std::move(other.worldPosition);
			worldRotation = std::move(other.worldRotation);
			worldScale = std::move(other.worldScale);
			localTransform = glm::mat4(glm::translate(glm::mat4(1.0f), localPosition) *
									   glm::mat4(localRotation) *
									   glm::scale(glm::mat4(1.0f), localScale));
			worldTransform = glm::mat4(glm::translate(glm::mat4(1.0f), worldPosition) *
									   glm::mat4(worldRotation) *
									   glm::scale(glm::mat4(1.0f), worldScale));
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

	void Transform::Update()
	{
		UpdateParentTransform();
	}

	void Transform::SetAsIdentity()
	{
		*this = m_Identity;
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

	void Transform::SetGameObject(GameObject* gameObject)
	{
		m_GameObject = gameObject;
	}

	GameObject* Transform::GetGameObject() const
	{
		return m_GameObject;
	}

	glm::mat4 Transform::GetWorldTransform()
	{
		return worldTransform;
	}

	glm::mat4 Transform::GetLocalTransform()
	{
		return localTransform;
	}

	void Transform::UpdateParentTransform()
	{
		localTransform = (glm::translate(glm::mat4(1.0f), localPosition) *
						  glm::mat4(localRotation) *
						  glm::scale(glm::mat4(1.0f), localScale));

		glm::vec3 localSkew;
		glm::vec4 localPerspective;
		glm::decompose(localTransform, localScale, localRotation, localPosition, localSkew, localPerspective);

		GameObject* parent = m_GameObject->GetParent();
		if (parent)
		{
			parent->GetTransform()->UpdateParentTransform();
		}
		else
		{
			UpdateChildTransforms();
		}
	}

	void Transform::UpdateChildTransforms()
	{
		// Our local matrix should already have been updated at this point (in UpdateParentTransform)

		GameObject* parent = m_GameObject->GetParent();
		if (parent)
		{
			Transform* parentTransform = parent->GetTransform();

			worldTransform = parentTransform->GetWorldTransform() * localTransform;

			glm::vec3 worldSkew;
			glm::vec4 worldPerspective;
			glm::decompose(worldTransform, worldScale, worldRotation, worldPosition, worldSkew, worldPerspective);
		}
		else
		{
			worldTransform = localTransform;
			worldPosition = localPosition;
			worldRotation = localRotation;
			worldScale = localScale;
		}

		const std::vector<GameObject*>& children = m_GameObject->GetChildren();
		for (auto iter = children.begin(); iter != children.end(); ++iter)
		{
			(*iter)->GetTransform()->UpdateChildTransforms();
		}
	}

	glm::vec3 Transform::GetLocalPosition() const
	{
		return localPosition;
	}

	glm::vec3 Transform::GetWorldPosition() const
	{
		return worldPosition;
	}

	glm::quat Transform::GetLocalRotation() const
	{
		return localRotation;
	}

	glm::quat Transform::GetWorldlRotation() const
	{
		return worldRotation;
	}

	glm::vec3 Transform::GetLocalScale() const
	{
		return localScale;
	}

	glm::vec3 Transform::GetWorldlScale() const
	{
		return worldScale;
	}

	void Transform::SetLocalPosition(const glm::vec3& position)
	{
		localPosition = position;

		UpdateParentTransform();
	}

	void Transform::SetWorldlPosition(const glm::vec3& position)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent)
		{
			localPosition = position - parent->GetTransform()->GetWorldPosition();
		}
		else
		{
			localPosition = position;
			// NOTE: World position will be set in UpdateParentTransform
		}
		
		UpdateParentTransform();
	}

	void Transform::SetLocalRotation(const glm::quat& quatRotation)
	{
		localRotation = quatRotation;

		UpdateParentTransform();
	}

	void Transform::SetWorldRotation(const glm::quat& quatRotation)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent)
		{
			localRotation = quatRotation - parent->GetTransform()->GetWorldlRotation();
		}
		else
		{
			localRotation = quatRotation;
			// World rotation will be set in UpdateParentTransform
		}

		UpdateParentTransform();
	}

	void Transform::SetLocalRotation(const glm::vec3& eulerAnglesRad)
	{
		localRotation = glm::quat(eulerAnglesRad);

		UpdateParentTransform();
	}

	void Transform::SetWorldRotation(const glm::vec3& eulerAnglesRad)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent)
		{
			localRotation = glm::quat(eulerAnglesRad) - parent->GetTransform()->GetWorldlRotation();
		}
		else
		{
			localRotation = glm::quat(eulerAnglesRad);
			// World rotation will be set in UpdateParentTransform
		}

		UpdateParentTransform();
	}

	void Transform::SetLocalRotation(real eulerXRad, real eulerYRad, real eulerZRad)
	{
		localRotation = glm::quat(glm::vec3(eulerXRad, eulerYRad, eulerZRad));

		UpdateParentTransform();
	}

	void Transform::SetWorldRotation(real eulerXRad, real eulerYRad, real eulerZRad)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent)
		{
			localRotation = glm::quat(glm::vec3(eulerXRad, eulerYRad, eulerZRad)) - parent->GetTransform()->GetWorldlRotation();
		}
		else
		{
			localRotation = glm::quat(glm::vec3(eulerXRad, eulerYRad, eulerZRad));
			// World rotation will be set in UpdateParentTransform
		}

		UpdateParentTransform();
	}

	void Transform::SetLocalScale(const glm::vec3& scale)
	{
		localScale = scale;

		UpdateParentTransform();
	}

	void Transform::SetWorldScale(const glm::vec3& scale)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent)
		{
			localScale = scale / parent->GetTransform()->GetWorldlScale();
		}
		else
		{
			localScale = scale;
			// World scale will be set in UpdateParentTransform
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

			SetWorldlPosition(pos);
			SetWorldRotation(rot);
		}
	}
} // namespace flex
