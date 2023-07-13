#include "stdafx.hpp"

#include "Physics/PhysicsTypeConversions.hpp"

#include "Transform.hpp"

btVector3 ToBtVec3(const glm::vec3& rhs)
{
	return btVector3(rhs.x, rhs.y, rhs.z);
}

btVector4 ToBtVec4(const glm::vec4& rhs)
{
	return btVector4(rhs.x, rhs.y, rhs.z, rhs.w);
}

btQuaternion ToBtQuaternion(const glm::quat& rhs)
{
	return btQuaternion(rhs.x, rhs.y, rhs.z, rhs.w);
}

glm::vec3 ToVec3(const btVector3& rhs)
{
	return glm::vec3(rhs.getX(), rhs.getY(), rhs.getZ());
}

glm::vec4 ToVec4(const btVector4& rhs)
{
	return glm::vec4(rhs.getX(), rhs.getY(), rhs.getZ(), rhs.getW());
}

glm::quat ToQuaternion(const btQuaternion& rhs)
{
	return glm::quat(rhs.getW(), rhs.getX(), rhs.getY(), rhs.getZ());
}

glm::mat4 BtMat3ToMat4(const btMatrix3x3& mat3)
{
	btVector3 row0 = mat3.getRow(0);
	btVector3 row1 = mat3.getRow(1);
	btVector3 row2 = mat3.getRow(2);
	return glm ::mat4(
		row0.x(), row0.y(), row0.z(), 0.0f,
		row1.x(), row1.y(), row1.z(), 0.0f,
		row2.x(), row2.y(), row2.z(), 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
}
