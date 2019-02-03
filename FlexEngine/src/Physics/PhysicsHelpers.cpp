#include "stdafx.hpp"

#include "Physics/PhysicsHelpers.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
IGNORE_WARNINGS_POP

namespace flex
{
	std::string CollisionShapeTypeToString(int shapeType)
	{
		for (u32 i = 0; i < ARRAY_LENGTH(g_CollisionTypes); ++i)
		{
			if ((int)g_CollisionTypes[i] == shapeType)
			{
				return g_CollisionTypeStrs[i];
			}
		}

		PrintError("Unhandled btCollisionShape type: %i\n", (i32)shapeType);
		return "UNHANDLED_BroadphaseNativeType";
	}

	BroadphaseNativeTypes StringToCollisionShapeType(const std::string& str)
	{
		const char* cStr = str.c_str();
		for (u32 i = 0; i < ARRAY_LENGTH(g_CollisionTypeStrs); ++i)
		{
			if (strcmp(g_CollisionTypeStrs[i], cStr) == 0)
			{
				return g_CollisionTypes[i];
			}
		}

		PrintError("Unhandled BroadphaseNativeTypes string: %s\n", str.c_str());
		return MAX_BROADPHASE_COLLISION_TYPES;
	}
} // namespace flex