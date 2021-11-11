#include "stdafx.hpp"

#include "Physics/PhysicsHelpers.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
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

	btCollisionShape* CloneCollisionShape(const glm::vec3& worldScale, btCollisionShape* originalShape)
	{
		btCollisionShape* newCollisionShape = nullptr;

		btVector3 btWorldScale = ToBtVec3(worldScale);
		real btWorldScaleX = btWorldScale.getX();

		i32 shapeType = originalShape->getShapeType();
		switch (shapeType)
		{
		case BOX_SHAPE_PROXYTYPE:
		{
			btVector3 btHalfExtents = static_cast<btBoxShape*>(originalShape)->getHalfExtentsWithMargin();
			btHalfExtents = btHalfExtents / btWorldScale;
			newCollisionShape = new btBoxShape(btHalfExtents);
		} break;
		case SPHERE_SHAPE_PROXYTYPE:
		{
			real radius = static_cast<btSphereShape*>(originalShape)->getRadius();
			radius /= btWorldScaleX;
			newCollisionShape = new btSphereShape(radius);
		} break;
		case CAPSULE_SHAPE_PROXYTYPE:
		{
			real radius = static_cast<btCapsuleShapeZ*>(originalShape)->getRadius();
			real height = static_cast<btCapsuleShapeZ*>(originalShape)->getHalfHeight() * 2.0f;
			radius /= btWorldScaleX;
			height /= btWorldScaleX;
			newCollisionShape = new btCapsuleShapeZ(radius, height);
		} break;
		case CONE_SHAPE_PROXYTYPE:
		{
			real radius = static_cast<btConeShape*>(originalShape)->getRadius();
			real height = static_cast<btConeShape*>(originalShape)->getHeight();
			radius /= btWorldScaleX;
			height /= btWorldScaleX;
			newCollisionShape = new btConeShape(radius, height);
		} break;
		case CYLINDER_SHAPE_PROXYTYPE:
		{
			btVector3 btHalfExtents = static_cast<btCylinderShape*>(originalShape)->getHalfExtentsWithMargin();
			btHalfExtents = btHalfExtents / btWorldScale;
			newCollisionShape = new btCylinderShape(btHalfExtents);
		} break;
		default:
		{
			PrintWarn("Unhanded shape type in GameObject::CopyGenericFields\n");
		} break;
		}

		// TODO: Copy constraints here too

		return newCollisionShape;
	}
} // namespace flex