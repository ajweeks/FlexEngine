#include "stdafx.hpp"

#include "Physics/PhysicsTypeConversions.hpp"

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

glm::vec3 FromBtVec3(const btVector3& rhs)
{
	return glm::vec3(rhs.getX(), rhs.getY(), rhs.getZ());
}

glm::vec4 FromBtVec4(const btVector4& rhs)
{
	return glm::vec4(rhs.getX(), rhs.getY(), rhs.getZ(), rhs.getW());
}

glm::quat FromBtQuaternion(const btQuaternion& rhs)
{
	return glm::quat(rhs.getW(), rhs.getX(), rhs.getY(), rhs.getZ());
}
