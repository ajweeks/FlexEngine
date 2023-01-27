#include "stdafx.hpp"

#include "Transform.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_operation.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "BulletCollision/CollisionShapes/btCollisionShape.h"
#include "BulletDynamics/Dynamics/btRigidBody.h"
IGNORE_WARNINGS_POP

#include "Helpers.hpp"
#include "Physics/RigidBody.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	const Transform Transform::Identity = Transform(VEC3_ZERO, QUAT_IDENTITY, VEC3_ONE);

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
		localScale(VEC3_ONE)
	{
	}

	Transform::Transform(const glm::vec3& position) :
		localPosition(position),
		localRotation(QUAT_IDENTITY),
		localScale(VEC3_ONE)
	{
	}

	Transform::Transform(const Transform& other) :
		localPosition(other.localPosition),
		localRotation(other.localRotation),
		localScale(other.localScale),
		m_GameObject(other.m_GameObject)
	{
	}

	Transform::Transform(const Transform&& other) :
		localPosition(other.localPosition),
		localRotation(other.localRotation),
		localScale(other.localScale),
		m_GameObject(other.m_GameObject)
	{
	}

	void Transform::ParseJSON(const JSONObject& object, Transform& outTransform)
	{
		glm::vec3 pos;
		glm::quat rot;
		glm::vec3 scale;
		ParseJSON(object, pos, rot, scale);
		outTransform.CloneFrom(Transform(pos, rot, scale));
	}

	void Transform::ParseJSON(const JSONObject& object, glm::mat4& outModel)
	{
		const JSONObject& transformObject = object.GetObject("transform");

		glm::vec3 pos;
		glm::quat rot;
		glm::vec3 scale;
		ParseJSON(transformObject, pos, rot, scale);

		outModel = glm::mat4(rot) *
			glm::diagonal4x4(glm::vec4(scale, 1.0f));
		outModel[3] = glm::vec4(pos, 1.0f);
	}

	void Transform::ParseJSON(const JSONObject& object, glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale)
	{
		if (!object.TryGetVec3("pos", outPos))
		{
			outPos = VEC3_ZERO;
		}

		if (!object.TryGetQuat("rot", outRot))
		{
			outRot = QUAT_IDENTITY;
		}

		if (!object.TryGetVec3("scale", outScale))
		{
			outScale = VEC3_ONE;
		}

		// Check we aren't getting garbage data in
#if DEBUG
		if (IsNanOrInf(outPos))
		{
			PrintError("Read garbage value from transform pos in serialized scene file! Using default value instead\n");
			outPos = VEC3_ZERO;
		}

		if (IsNanOrInf(outRot))
		{
			PrintError("Read garbage value from transform rot in serialized scene file! Using default value instead\n");
			outRot = QUAT_IDENTITY;
		}

		if (IsNanOrInf(outScale))
		{
			PrintError("Read garbage value from transform scale in serialized scene file! Using default value instead\n");
			outScale = VEC3_ONE;
		}
#endif
	}

	void Transform::CloneFrom(const Transform& other)
	{
		localPosition = other.localPosition;
		localRotation = other.localRotation;
		localScale = other.localScale;
		cachedWorldTransform = other.cachedWorldTransform;
		bDirty = other.bDirty;

		// NOTE: m_GameObject is not copied here
	}

	JSONObject Transform::Serialize() const
	{
		return Serialize(localPosition, localRotation, localScale, m_GameObject->GetName().c_str());
	}

	JSONObject Transform::Serialize(const glm::mat4 matrix, const char* objName)
	{
		glm::vec3 pos;
		glm::quat rot;
		glm::vec3 scale;
		Decompose(matrix, pos, rot, scale);

		return Serialize(pos, rot, scale, objName);
	}

	JSONObject Transform::Serialize(const glm::vec3& inPos, const glm::quat& inRot, const glm::vec3& inScale, const char* objName)
	{
		const i32 floatPrecision = 3;

		glm::vec3 pos = inPos;
		glm::quat rot = inRot;
		glm::vec3 scale = inScale;

		JSONObject transformObject = {};

		if (IsNanOrInf(pos))
		{
			PrintError("Attempted to serialize garbage value for position of %s, writing default value\n", objName);
			pos = VEC3_ZERO;
		}

		if (IsNanOrInf(rot))
		{
			PrintError("Attempted to serialize garbage value for rotation of %s, writing default value\n", objName);
			rot = glm::quat();
		}

		if (IsNanOrInf(scale))
		{
			PrintError("Attempted to serialize garbage value for scale of %s, writing default value\n", objName);
			scale = VEC3_ONE;
		}


		if (pos != VEC3_ZERO)
		{
			std::string posStr = VecToString(pos, floatPrecision);
			transformObject.fields.emplace_back("pos", JSONValue(posStr));
		}

		if (rot != QUAT_IDENTITY)
		{
			glm::vec3 rotEuler = glm::eulerAngles(rot);
			std::string rotStr = VecToString(rotEuler, floatPrecision);
			transformObject.fields.emplace_back("rot", JSONValue(rotStr));
		}

		if (scale != VEC3_ONE)
		{
			std::string scaleStr = VecToString(scale, floatPrecision);
			transformObject.fields.emplace_back("scale", JSONValue(scaleStr));
		}

		return transformObject;
	}

	void Transform::SetAsIdentity()
	{
		CloneFrom(Identity);
	}

	bool Transform::IsIdentity() const
	{
		bool result = (localPosition == Identity.localPosition &&
			localRotation == Identity.localRotation &&
			localScale == Identity.localScale);
		return result;
	}

	glm::vec3 Transform::GetLocalPosition() const
	{
		return localPosition;
	}

	glm::vec3 Transform::GetWorldPosition() const
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent != nullptr)
		{
			glm::mat4 m = parent->GetTransform()->GetWorldTransform();
			return glm::vec3(m * glm::vec4(localPosition, 1.0f));
		}
		return localPosition;
	}

	glm::quat Transform::GetLocalRotation() const
	{
		return localRotation;
	}

	glm::quat Transform::GetWorldRotation() const
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent != nullptr)
		{
			glm::mat4 m = parent->GetTransform()->GetWorldTransform();
			return glm::quat_cast(m) * localRotation;
		}
		return localRotation;
	}

	glm::vec3 Transform::GetLocalScale() const
	{
		return localScale;
	}

	glm::vec3 Transform::GetWorldScale() const
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent != nullptr)
		{
			return parent->GetTransform()->GetWorldScale() * localScale;
		}
		return localScale;
	}

	glm::vec3 Transform::GetRight() const
	{
		return GetWorldRotation() * VEC3_RIGHT;
	}

	glm::vec3 Transform::GetUp() const
	{
		return GetWorldRotation() * VEC3_UP;
	}

	glm::vec3 Transform::GetForward() const
	{
		return GetWorldRotation() * VEC3_FORWARD;
	}

	void Transform::SetLocalPosition(const glm::vec3& position, bool bUpdateChain /* = true */)
	{
		localPosition = position;

		MarkDirty();

		if (bUpdateChain)
		{
			Recompute();

			UpdateRigidBody();
		}
	}

	void Transform::UpdateRigidBody()
	{
		RigidBody* rigidBody = m_GameObject->GetRigidBody();
		if (rigidBody != nullptr)
		{
			glm::vec3 worldScale = GetWorldScale();
			if (!NearlyEquals(worldScale, VEC3_ONE, 0.00001f))
			{
				rigidBody->GetRigidBodyInternal()->getCollisionShape()->setLocalScaling(ToBtVec3(worldScale));
			}
			rigidBody->SetWorldPositionAndRotation(GetWorldPosition(), GetWorldRotation());
		}

		u32 childCount = m_GameObject->GetChildCount();
		for (u32 i = 0; i < childCount; ++i)
		{
			m_GameObject->GetChild(i)->GetTransform()->UpdateRigidBody();
		}
	}

	void Transform::SetWorldPosition(const glm::vec3& position, bool bUpdateChain /* = true */)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent != nullptr)
		{
			localPosition = position - parent->GetTransform()->GetWorldPosition();
		}
		else
		{
			localPosition = position;
		}

		MarkDirty();

		if (bUpdateChain)
		{
			Recompute();

			UpdateRigidBody();
		}
	}

	void Transform::SetLocalRotation(const glm::quat& rotation, bool bUpdateChain /* = true */)
	{
		localRotation = rotation;

		MarkDirty();

		if (bUpdateChain)
		{
			Recompute();

			UpdateRigidBody();
		}
	}

	void Transform::SetWorldRotation(const glm::quat& rotation, bool bUpdateChain /* = true */)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent != nullptr)
		{
			localRotation = glm::inverse(parent->GetTransform()->GetWorldRotation()) * rotation;
		}
		else
		{
			localRotation = rotation;
		}

		MarkDirty();

		if (bUpdateChain)
		{
			Recompute();

			UpdateRigidBody();
		}
	}

	void Transform::SetLocalScale(const glm::vec3& scale, bool bUpdateChain /* = true */)
	{
		localScale = scale;

		MarkDirty();

		if (bUpdateChain)
		{
			Recompute();

			UpdateRigidBody();
		}
	}

	void Transform::SetWorldScale(const glm::vec3& scale, bool bUpdateChain /* = true */)
	{
		GameObject* parent = m_GameObject->GetParent();
		if (parent != nullptr)
		{
			localScale = scale / parent->GetTransform()->GetWorldScale();
		}
		else
		{
			localScale = scale;
		}

		MarkDirty();

		if (bUpdateChain)
		{
			Recompute();

			UpdateRigidBody();
		}
	}

	void Transform::SetWorldFromMatrix(const glm::mat4& mat, bool bUpdateChain /* = true */)
	{
		glm::vec3 pos;
		glm::quat rot;
		glm::vec3 scale;
		Decompose(mat, pos, rot, scale);

		SetWorldPosition(pos, false);
		SetWorldRotation(rot, false);
		SetWorldScale(scale, bUpdateChain);
		cachedWorldTransform = mat;
		bDirty = false;
	}

	void Transform::Translate(const glm::vec3& deltaPosition)
	{
		Translate(deltaPosition.x, deltaPosition.y, deltaPosition.z);
	}

	void Transform::Translate(real deltaX, real deltaY, real deltaZ)
	{
		localPosition.x += deltaX;
		localPosition.y += deltaY;
		localPosition.z += deltaZ;

		RigidBody* rigidBody = m_GameObject->GetRigidBody();
		if (rigidBody != nullptr)
		{
			// Force rigid body to update to new position
			rigidBody->SetWorldPosition(GetWorldPosition());
		}

		const std::vector<GameObject*>& children = m_GameObject->GetChildren();
		for (GameObject* child : children)
		{
			Transform* childTransform = child->GetTransform();
			childTransform->SetLocalPosition(childTransform->GetLocalPosition());
		}
		MarkDirty();
	}

	void Transform::Rotate(const glm::quat& deltaRotation)
	{
		localRotation *= deltaRotation;

		RigidBody* rigidBody = m_GameObject->GetRigidBody();
		if (rigidBody != nullptr)
		{
			// Force rigid body to update to new rotation
			rigidBody->SetWorldRotation(GetWorldRotation());
		}

		u32 childCount = m_GameObject->GetChildCount();
		for (u32 i = 0; i < childCount; ++i)
		{
			Transform* childTransform = m_GameObject->GetChild(i)->GetTransform();
			childTransform->SetLocalRotation(childTransform->GetLocalRotation());
		}
		MarkDirty();
	}

	void Transform::Scale(const glm::vec3& deltaScale)
	{
		localScale *= deltaScale;
		MarkDirty();
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
		GameObject* parent = m_GameObject->GetParent();
		glm::mat4 localTransform;
		if (parent != nullptr)
		{
			localTransform = glm::inverse(parent->GetTransform()->GetWorldTransform()) * desiredWorldTransform;
		}
		else
		{
			localTransform = desiredWorldTransform;
		}

		Decompose(localTransform, localPosition, localRotation, localScale);
		cachedWorldTransform = desiredWorldTransform;
		bDirty = false;
	}

	void Transform::SetFromBtTransform(const btTransform& transform)
	{
		OnRigidbodyTransformChanged(ToVec3(transform.getOrigin()), ToQuaternion(transform.getRotation()));
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
		if (!bDirty)
		{
			return cachedWorldTransform;
		}

		Recompute();

		return cachedWorldTransform;
	}

	void Transform::Recompute()
	{
		CHECK_EQ(bDirty, true);

		glm::mat4 localTransform = glm::mat4(localRotation) *
			glm::diagonal4x4(glm::vec4(localScale, 1.0f));
		localTransform[3] = glm::vec4(localPosition, 1.0f);

		GameObject* parent = m_GameObject->GetParent();
		if (parent != nullptr)
		{
			cachedWorldTransform = parent->GetTransform()->GetWorldTransform() * localTransform;
		}
		else
		{
			cachedWorldTransform = localTransform;
		}

		bDirty = false;
	}

	void Transform::MarkDirty()
	{
		bDirty = true;

		u32 childCount = m_GameObject->GetChildCount();
		for (u32 i = 0; i < childCount; ++i)
		{
			Transform* childTransform = m_GameObject->GetChild(i)->GetTransform();
			childTransform->MarkDirty();
		}
	}

	void Transform::Decompose(const glm::mat4& mat, glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale)
	{
		outPos = glm::vec3(mat[3]);

		outScale.x = glm::length(mat[0]);
		outScale.y = glm::length(mat[1]);
		outScale.z = glm::length(mat[2]);

		glm::vec3 matRow0 = glm::vec3(mat[0]) / outScale.x;
		glm::vec3 matRow1 = glm::vec3(mat[1]) / outScale.y;
		glm::vec3 matRow2 = glm::vec3(mat[2]) / outScale.z;

		// Extract quaternion (taken from glm/gtx/matrix_decompose.inl)
		real trace = mat[0].x + mat[1].y + mat[2].z;
		if (trace > 0.0f)
		{
			real root = glm::sqrt(trace + 1.0f);
			outRot.w = 0.5f * root;
			root = 0.5f / root;
			outRot.x = root * (matRow1.z - matRow2.y);
			outRot.y = root * (matRow2.x - matRow0.z);
			outRot.z = root * (matRow0.y - matRow1.x);
		}
		else
		{
			static i32 next[3] = { 1, 2, 0 };
			i32 i = 0;
			if (matRow1.y > matRow0.x) i = 1;
			if (matRow2.z > mat[i][i]) i = 2;
			i32 j = next[i];
			i32 k = next[j];

			real root = glm::sqrt(mat[i][i] - mat[j][j] - mat[k][k] + 1.0f);

			outRot[i] = 0.5f * root;
			root = 0.5f / root;
			outRot[j] = root * (mat[i][j] + mat[j][i]);
			outRot[k] = root * (mat[i][k] + mat[k][i]);
			outRot.w = root * (mat[j][k] - mat[k][j]);
		}
	}

	void Transform::OnRigidbodyTransformChanged(const glm::vec3& position, const glm::quat& rotation)
	{
		GameObject* parent = m_GameObject->GetParent();
		glm::vec3 newPosition;
		glm::quat newRotation;
		if (parent != nullptr)
		{
			newPosition = position - parent->GetTransform()->GetWorldPosition();
			newRotation = glm::inverse(parent->GetTransform()->GetWorldRotation()) * rotation;

		}
		else
		{
			newPosition = position;
			newRotation = rotation;
		}

		if (!NearlyEquals(localPosition, newPosition, 0.00001f) ||
			!NearlyEquals(localRotation, newRotation, 0.00001f))
		{
			localPosition = newPosition;
			localRotation = newRotation;
			MarkDirty();
		}
	}
} // namespace flex
