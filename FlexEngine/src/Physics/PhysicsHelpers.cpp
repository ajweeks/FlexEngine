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

#include <JSONTypes.hpp>

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

	btCollisionShape* CreateCollisionShape(BroadphaseNativeTypes collisionShapeType,
		float optionalHalfExtentsX /* = -1.0f */,
		float optionalHalfExtentsY /* = -1.0f */,
		float optionalHalfExtentsZ /* = -1.0f */)
	{
		btVector3 halfExtents(
			optionalHalfExtentsX != -1.0f ? optionalHalfExtentsX : 0.5f,
			optionalHalfExtentsY != -1.0f ? optionalHalfExtentsY : 0.5f,
			optionalHalfExtentsZ != -1.0f ? optionalHalfExtentsZ : 0.5f);

		switch (collisionShapeType)
		{
		case BOX_SHAPE_PROXYTYPE:
			return new btBoxShape(halfExtents);
		case SPHERE_SHAPE_PROXYTYPE:
			return new btSphereShape(halfExtents.x());
		case CAPSULE_SHAPE_PROXYTYPE:
			return new btCapsuleShapeZ(halfExtents.x(), halfExtents.y());
		case CYLINDER_SHAPE_PROXYTYPE:
			return new btCylinderShape(halfExtents);
		case CONE_SHAPE_PROXYTYPE:
			return new btConeShape(halfExtents.x(), halfExtents.y());
		default:
			PrintError("Unhandled BroadphaseNativeType in CreateCollisionShape: %d\n", (i32)collisionShapeType);
			return nullptr;
		}
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

	bool SerializeCollider(btCollisionShape* collisionShape, const glm::vec3& scaleWS, JSONObject& outColliderObj)
	{
		i32 shapeType = collisionShape->getShapeType();
		std::string shapeTypeStr = CollisionShapeTypeToString(shapeType);
		outColliderObj.fields.emplace_back("shape", JSONValue(shapeTypeStr));

		switch (shapeType)
		{
		case BOX_SHAPE_PROXYTYPE:
		{
			btVector3 btHalfExtents = static_cast<btBoxShape*>(collisionShape)->getHalfExtentsWithMargin();
			glm::vec3 halfExtents = ToVec3(btHalfExtents);
			halfExtents /= scaleWS;
			outColliderObj.fields.emplace_back("half extents", JSONValue(halfExtents, 3));
		} return true;
		case SPHERE_SHAPE_PROXYTYPE:
		{
			real radius = static_cast<btSphereShape*>(collisionShape)->getRadius();
			radius /= scaleWS.x;
			outColliderObj.fields.emplace_back("radius", JSONValue(radius));
		} return true;
		case CAPSULE_SHAPE_PROXYTYPE:
		{
			real radius = static_cast<btCapsuleShapeZ*>(collisionShape)->getRadius();
			real height = static_cast<btCapsuleShapeZ*>(collisionShape)->getHalfHeight() * 2.0f;
			radius /= scaleWS.x;
			height /= scaleWS.x;
			outColliderObj.fields.emplace_back("radius", JSONValue(radius));
			outColliderObj.fields.emplace_back("height", JSONValue(height));
		} return true;
		case CONE_SHAPE_PROXYTYPE:
		{
			real radius = static_cast<btConeShape*>(collisionShape)->getRadius();
			real height = static_cast<btConeShape*>(collisionShape)->getHeight();
			radius /= scaleWS.x;
			height /= scaleWS.x;
			outColliderObj.fields.emplace_back("radius", JSONValue(radius));
			outColliderObj.fields.emplace_back("height", JSONValue(height));
		} return true;
		case CYLINDER_SHAPE_PROXYTYPE:
		{
			btVector3 btHalfExtents = static_cast<btCylinderShape*>(collisionShape)->getHalfExtentsWithMargin();
			glm::vec3 halfExtents = ToVec3(btHalfExtents);
			halfExtents /= scaleWS.x;
			outColliderObj.fields.emplace_back("half extents", JSONValue(halfExtents, 3));
		} return true;
		default:
			return false;
		}
	}

	btCollisionShape* ParseCollider(const JSONObject& colliderObj)
	{
		std::string shapeStr = colliderObj.GetString("shape");
		BroadphaseNativeTypes shapeType = StringToCollisionShapeType(shapeStr);

		switch (shapeType)
		{
		case BOX_SHAPE_PROXYTYPE:
		{
			glm::vec3 halfExtents = colliderObj.GetVec3("half extents");
			btVector3 btHalfExtents(halfExtents.x, halfExtents.y, halfExtents.z);
			return new btBoxShape(btHalfExtents);
		} break;
		case SPHERE_SHAPE_PROXYTYPE:
		{
			real radius = colliderObj.GetFloat("radius");
			return new btSphereShape(radius);
		} break;
		case CAPSULE_SHAPE_PROXYTYPE:
		{
			real radius = colliderObj.GetFloat("radius");
			real height = colliderObj.GetFloat("height");
			return new btCapsuleShapeZ(radius, height);
		} break;
		case CONE_SHAPE_PROXYTYPE:
		{
			real radius = colliderObj.GetFloat("radius");
			real height = colliderObj.GetFloat("height");
			return new btConeShape(radius, height);
		} break;
		case CYLINDER_SHAPE_PROXYTYPE:
		{
			glm::vec3 halfExtents = colliderObj.GetVec3("half extents");
			btVector3 btHalfExtents(halfExtents.x, halfExtents.y, halfExtents.z);
			return new btCylinderShape(btHalfExtents);
		} break;
		default:
		{
			PrintWarn("Unhandled BroadphaseNativeType: %s\n", shapeStr.c_str());
			return nullptr;
		} break;
		}
	}
} // namespace flex