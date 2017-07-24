#include "stdafx.h"

#include "Prefabs\Transform.h"

using namespace glm;

Transform::Transform()
{
	SetAsIdentity();
}

Transform::Transform(const vec3& position, const quat& rotation, const vec3& scale) :
	position(position),
	rotation(rotation),
	scale(scale)
{
}

Transform::~Transform()
{
}

void Transform::Translate(glm::vec3 deltaPosition)
{
	position += deltaPosition;
}

void Transform::Rotate(glm::quat deltaRotation)
{
	rotation *= deltaRotation;
}

void Transform::Rotate(glm::vec3 deltaEulerRotationRad)
{
	quat rotationQuat(deltaEulerRotationRad);
	rotation *= rotationQuat;
}

void Transform::Scale(glm::vec3 deltaScale)
{
	scale *= deltaScale;
}

void Transform::SetAsIdentity()
{
	position = vec3(0.0f);
	rotation = quat(vec3(0.0f));
	scale = vec3(1.0f);
}

Transform Transform::Identity()
{
	Transform result;
	
	result.SetAsIdentity();

	return result;
}
