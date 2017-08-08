#pragma once

#include <glm\vec3.hpp>
#include <glm\gtc\quaternion.hpp>

struct Transform
{
	Transform();
	Transform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);
	Transform(const glm::vec3& position, const glm::quat& rotation);
	Transform(const glm::vec3& position);
	~Transform();

	void Translate(glm::vec3 deltaPosition);
	void Rotate(glm::quat deltaRotation);
	void Rotate(glm::vec3 deltaEulerRotationRad);
	void Scale(glm::vec3 deltaScale);
	void SetAsIdentity();

	glm::mat4 GetModelMatrix();

	static Transform Identity();

	glm::vec3 position;
	glm::quat rotation;
	glm::vec3 scale;

};