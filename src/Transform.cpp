#include "stdafx.h"

#include "Transform.h"

#include <glm\gtc\matrix_transform.hpp>

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

glm::mat4 Transform::GetModelMatrix()
{
	glm::mat4 matTrans = glm::translate(glm::mat4(1.0f), position);
	glm::mat4 matRot = glm::mat4(rotation);
	glm::mat4 matScale = glm::scale(glm::mat4(1.0f), scale);
	glm::mat4 matModel = matTrans * matRot * matScale;

	return matModel;
}

Transform Transform::Identity()
{
	Transform result;
	
	result.SetAsIdentity();

	return result;
}
