#pragma once

#pragma warning(push, 0)
#include <LinearMath/btVector3.h>
#include <LinearMath/btQuaternion.h>
#include <LinearMath/btTransform.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>
#pragma warning(pop)

#include "Transform.hpp"

btVector3 Vec3ToBtVec3(const glm::vec3& rhs);
btVector4 Vec4ToBtVec4(const glm::vec4& rhs);
btQuaternion QuaternionToBtQuaternion(const glm::quat& rhs);

glm::vec3 BtVec3ToVec3(const btVector3& rhs);
glm::vec4 BtVec4ToVec4(const btVector4& rhs);
glm::quat BtQuaternionToQuaternion(const btQuaternion& rhs);

btTransform TransformToBtTransform(const flex::Transform& transform);
flex::Transform BtTransformToTransform(const btTransform& transform);
