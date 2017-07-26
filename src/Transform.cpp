#include "stdafx.h"

#include "Transform.h"

#include <glm\gtc\matrix_transform.hpp>

Transform::Transform()
{
	SetAsIdentity();
}

Transform::Transform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) :
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
	glm::quat rotationQuat(deltaEulerRotationRad);
	rotation *= rotationQuat;
}

void Transform::Scale(glm::vec3 deltaScale)
{
	scale *= deltaScale;
}

void Transform::SetAsIdentity()
{
	position = glm::vec3(0.0f);
	rotation = glm::quat(glm::vec3(0.0f));
	scale = glm::vec3(1.0f);
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
