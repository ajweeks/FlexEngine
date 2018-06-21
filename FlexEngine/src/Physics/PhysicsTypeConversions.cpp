#include "stdafx.hpp"

#include "Physics/PhysicsTypeConversions.hpp"

btVector3 Vec3ToBtVec3(const glm::vec3& rhs)
{
	return btVector3(rhs.x, rhs.y, rhs.z);
}

btVector4 Vec4ToBtVec4(const glm::vec4& rhs)
{
	return btVector4(rhs.x, rhs.y, rhs.z, rhs.w);
}

btQuaternion QuaternionToBtQuaternion(const glm::quat& rhs)
{
	return btQuaternion(rhs.x, rhs.y, rhs.z, rhs.w);
}

glm::vec3 BtVec3ToVec3(const btVector3& rhs)
{
	return glm::vec3(rhs.getX(), rhs.getY(), rhs.getZ());
}

glm::vec4 BtVec4ToVec4(const btVector4& rhs)
{
	return glm::vec4(rhs.getX(), rhs.getY(), rhs.getZ(), rhs.getW());
}

glm::quat BtQuaternionToQuaternion(const btQuaternion& rhs)
{
	return glm::quat(rhs.getW(), rhs.getX(), rhs.getY(), rhs.getZ());
}

btTransform TransformToBtTransform(const flex::Transform& transform)
{
	btTransform result(QuaternionToBtQuaternion(transform.GetWorldlRotation()), Vec3ToBtVec3(transform.GetWorldPosition()));
	return result;
}

flex::Transform BtTransformToTransform(const btTransform& transform)
{
	flex::Transform result(BtVec3ToVec3(transform.getOrigin()), BtQuaternionToQuaternion(transform.getRotation()));
	return result;
}
