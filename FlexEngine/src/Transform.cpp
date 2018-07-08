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

	Transform Transform::ParseJSON(const JSONObject& object)
	{
		std::string posStr = object.GetString("position");
		std::string rotStr = object.GetString("rotation");
		std::string scaleStr = object.GetString("scale");

		glm::vec3 pos(0.0f);
		if (!posStr.empty())
		{
			pos = ParseVec3(posStr);
		}

		glm::vec3 rotEuler(0.0f);
		if (!rotStr.empty())
		{
			rotEuler = ParseVec3(rotStr);
		}

		glm::vec3 scale(1.0f);
		if (!scaleStr.empty())
		{
			scale = ParseVec3(scaleStr);
		}

		// Check we aren't getting garbage data in
#if _DEBUG
		if (IsNanOrInf(pos))
		{
			Logger::LogError("Read garbage value from transform pos in serialized scene file! Using default value instead");
			pos = glm::vec3(0.0f);
		}

		if (IsNanOrInf(rotEuler))
		{
			Logger::LogError("Read garbage value from transform rot in serialized scene file! Using default value instead");
			rotEuler = glm::vec3(0.0f);
		}

		if (IsNanOrInf(scale))
		{
			Logger::LogError("Read garbage value from transform scale in serialized scene file! Using default value instead");
			scale = glm::vec3(1.0f);
		}
#endif

		glm::quat rotQuat = glm::quat(rotEuler);

		return Transform(pos, rotQuat, scale);
	}

	JSONField Transform::SerializeToJSON()
	{
		JSONField transformField = {};
		transformField.label = "transform";

		JSONObject transformObject = {};

		if (IsNanOrInf(localPosition))
		{
			Logger::LogError("Attempted to serialize garbage value for " + m_GameObject->GetName() + "'s pos - writing default value instead");
			localPosition = glm::vec3(0.0f);
		}

		if (IsNanOrInf(localRotation))
		{
			Logger::LogError("Attempted to serialize garbage value for " + m_GameObject->GetName() + "'s rot - writing default value instead");
			localRotation = glm::quat();
		}

		if (IsNanOrInf(localScale))
		{
			Logger::LogError("Attempted to serialize garbage value for " + m_GameObject->GetName() + "'s scale - writing default value instead");
			localScale = glm::vec3(1.0f);
		}

		glm::vec3 localRotEuler = glm::eulerAngles(localRotation);

		std::string posStr = Vec3ToString(localPosition);
		std::string rotStr = Vec3ToString(localRotEuler);
		std::string scaleStr = Vec3ToString(localScale);

		transformObject.fields.push_back(JSONField("position", JSONValue(posStr)));
		transformObject.fields.push_back(JSONField("rotation", JSONValue(rotStr)));
		transformObject.fields.push_back(JSONField("scale", JSONValue(scaleStr)));

		transformField.value = JSONValue(transformObject);

		return transformField;
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

	void Transform::SetWorldTransform(const glm::mat4& desiredWorldTransform)
	{
		if (m_GameObject->GetParent())
		{
			localTransform = glm::inverse(m_GameObject->GetParent()->GetTransform()->GetWorldTransform()) * desiredWorldTransform;
		}
		else
		{
			localTransform = desiredWorldTransform;
		}

		glm::vec3 localSkew;
		glm::vec4 localPerspective;
		glm::decompose(localTransform, localScale, localRotation, localPosition, localSkew, localPerspective);

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
		// TODO: Move this
		localTransform = (glm::translate(glm::mat4(1.0f), localPosition) *
						  glm::mat4(localRotation) *
						  glm::scale(glm::mat4(1.0f), localScale));
		
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

		if (m_GameObject->GetRigidBody())
		{
			m_GameObject->GetRigidBody()->MatchParentTransform();
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

	glm::quat Transform::GetWorldRotation() const
	{
		return worldRotation;
	}

	glm::vec3 Transform::GetLocalScale() const
	{
		return localScale;
	}

	glm::vec3 Transform::GetWorldScale() const
	{
		return worldScale;
	}

	void Transform::SetLocalPosition(const glm::vec3& position, bool bUpdateChain /* = true */)
	{
		localPosition = position;

		if (bUpdateChain)
		{
			UpdateParentTransform();
		}
	}

	void Transform::SetWorldPosition(const glm::vec3& position, bool bUpdateChain /* = true */)
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
		
		if (bUpdateChain)
		{
			UpdateParentTransform();
		}
	}

	void Transform::SetLocalRotation(const glm::quat& quatRotation, bool bUpdateChain /* = true */)
	{
		localRotation = quatRotation;

		if (bUpdateChain)
		{
			UpdateParentTransform();
		}
	}

	void Transform::SetWorldRotation(const glm::quat& quatRotation, bool bUpdateChain /* = true */)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent)
		{
			localRotation = quatRotation - parent->GetTransform()->GetWorldRotation();
		}
		else
		{
			localRotation = quatRotation;
			// World rotation will be set in UpdateParentTransform
		}

		if (bUpdateChain)
		{
			UpdateParentTransform();
		}
	}

	void Transform::SetLocalRotation(const glm::vec3& eulerAnglesRad, bool bUpdateChain /* = true */)
	{
		localRotation = glm::quat(eulerAnglesRad);

		if (bUpdateChain)
		{
			UpdateParentTransform();
		}
	}

	void Transform::SetWorldRotation(const glm::vec3& eulerAnglesRad, bool bUpdateChain /* = true */)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent)
		{
			localRotation = glm::quat(eulerAnglesRad) - parent->GetTransform()->GetWorldRotation();
		}
		else
		{
			localRotation = glm::quat(eulerAnglesRad);
			// World rotation will be set in UpdateParentTransform
		}

		if (bUpdateChain)
		{
			UpdateParentTransform();
		}
	}

	void Transform::SetLocalRotation(real eulerXRad, real eulerYRad, real eulerZRad, bool bUpdateChain /* = true */)
	{
		localRotation = glm::quat(glm::vec3(eulerXRad, eulerYRad, eulerZRad));

		if (bUpdateChain)
		{
			UpdateParentTransform();
		}
	}

	void Transform::SetWorldRotation(real eulerXRad, real eulerYRad, real eulerZRad, bool bUpdateChain /* = true */)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent)
		{
			localRotation = glm::quat(glm::vec3(eulerXRad, eulerYRad, eulerZRad)) - parent->GetTransform()->GetWorldRotation();
		}
		else
		{
			localRotation = glm::quat(glm::vec3(eulerXRad, eulerYRad, eulerZRad));
			// World rotation will be set in UpdateParentTransform
		}

		if (bUpdateChain)
		{
			UpdateParentTransform();
		}
	}

	void Transform::SetLocalScale(const glm::vec3& scale, bool bUpdateChain /* = true */)
	{
		localScale = scale;

		if (bUpdateChain)
		{
			UpdateParentTransform();
		}
	}

	void Transform::SetWorldScale(const glm::vec3& scale, bool bUpdateChain /* = true */)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent)
		{
			localScale = scale / parent->GetTransform()->GetWorldScale();
		}
		else
		{
			localScale = scale;
			// World scale will be set in UpdateParentTransform
		}

		if (bUpdateChain)
		{
			UpdateParentTransform();
		}
	}

	void Transform::SetWorldFromMatrix(const glm::mat4& mat, bool bUpdateChain /* = true */)
	{
		glm::vec3 pos;
		glm::quat rot;
		glm::vec3 scale;
		glm::vec3 skew;
		glm::vec4 perspective;
		if (glm::decompose(mat, scale, rot, pos, skew, perspective))
		{
			SetWorldPosition(pos, false);
			SetWorldRotation(rot, false);
			SetWorldScale(scale, bUpdateChain);
		}
	}
} // namespace flex
