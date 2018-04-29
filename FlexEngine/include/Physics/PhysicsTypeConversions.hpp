#pragma once

#pragma warning(push, 0)
#include <LinearMath/btVector3.h>
#include <LinearMath/btQuaternion.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>
#pragma warning(pop)

btVector3 ToBtVec3(const glm::vec3& rhs);
btVector4 ToBtVec4(const glm::vec4& rhs);
btQuaternion ToBtQuaternion(const glm::quat& rhs);

glm::vec3 FromBtVec3(const btVector3& rhs);
glm::vec4 FromBtVec4(const btVector4& rhs);
glm::quat FromBtQuaternion(const btQuaternion& rhs);
