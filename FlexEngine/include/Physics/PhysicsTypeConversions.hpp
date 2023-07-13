#pragma once

IGNORE_WARNINGS_PUSH
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <LinearMath/btQuaternion.h>
#include <LinearMath/btTransform.h>
#include <LinearMath/btVector3.h>
IGNORE_WARNINGS_POP

namespace flex
{
	class Transform;
}

btVector3 ToBtVec3(const glm::vec3& rhs);
btVector4 ToBtVec4(const glm::vec4& rhs);
btQuaternion ToBtQuaternion(const glm::quat& rhs);

glm::vec3 ToVec3(const btVector3& rhs);
glm::vec4 ToVec4(const btVector4& rhs);
glm::quat ToQuaternion(const btQuaternion& rhs);
glm::mat4 BtMat3ToMat4(const btMatrix3x3& mat3);
