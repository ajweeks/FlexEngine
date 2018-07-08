#include "stdafx.hpp"

#include "Physics/PhysicsHelpers.hpp"

#pragma warning(push, 0)
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#pragma warning(pop)


namespace flex
{
	std::string CollisionShapeTypeToString(int shapeType)
	{
		switch (shapeType)
		{
		case BOX_SHAPE_PROXYTYPE:					return "box";
		case TRIANGLE_SHAPE_PROXYTYPE:				return "triangle";
		case TETRAHEDRAL_SHAPE_PROXYTYPE:			return "tetrahedral";
		case CONVEX_TRIANGLEMESH_SHAPE_PROXYTYPE:	return "convex triangle mesh";
		case SPHERE_SHAPE_PROXYTYPE:				return "sphere";
		case MULTI_SPHERE_SHAPE_PROXYTYPE:			return "multi sphere";
		case CAPSULE_SHAPE_PROXYTYPE:				return "capsule";
		case CONE_SHAPE_PROXYTYPE:					return "cone";
		case CYLINDER_SHAPE_PROXYTYPE:				return "cylinder";
		case TRIANGLE_MESH_SHAPE_PROXYTYPE:			return "triangle mesh";
		case TERRAIN_SHAPE_PROXYTYPE:				return "terrain";
		case STATIC_PLANE_PROXYTYPE:				return "static plane";
		case SOFTBODY_SHAPE_PROXYTYPE:				return "soft body";
		default:
			PrintError("Unhandled btCollisionShape type: %i\n", (i32)shapeType);
			return "UNHANDLED BroadphsaeNativeTypes: " + shapeType;
		}
	}

	BroadphaseNativeTypes StringToCollisionShapeType(const std::string& str)
	{
		if (str.compare("box") == 0)
		{
			return BOX_SHAPE_PROXYTYPE;
		}
		else if (str.compare("triangle") == 0)
		{
			return TRIANGLE_SHAPE_PROXYTYPE;
		}
		else if (str.compare("tetrahedral") == 0)
		{
			return TETRAHEDRAL_SHAPE_PROXYTYPE;
		}
		else if (str.compare("convex triangle mesh") == 0)
		{
			return CONVEX_TRIANGLEMESH_SHAPE_PROXYTYPE;
		}
		else if (str.compare("sphere") == 0)
		{
			return SPHERE_SHAPE_PROXYTYPE;
		}
		else if (str.compare("multi sphere") == 0)
		{
			return MULTI_SPHERE_SHAPE_PROXYTYPE;
		}
		else if (str.compare("capsule") == 0)
		{
			return CAPSULE_SHAPE_PROXYTYPE;
		}
		else if (str.compare("cone") == 0)
		{
			return CONE_SHAPE_PROXYTYPE;
		}
		else if (str.compare("cylinder") == 0)
		{
			return CYLINDER_SHAPE_PROXYTYPE;
		}
		else if (str.compare("triangle mesh") == 0)
		{
			return TRIANGLE_MESH_SHAPE_PROXYTYPE;
		}
		else if (str.compare("terrain") == 0)
		{
			return TERRAIN_SHAPE_PROXYTYPE;
		}
		else if (str.compare("static plane") == 0)
		{
			return STATIC_PLANE_PROXYTYPE;
		}
		else if (str.compare("soft body") == 0)
		{
			return SOFTBODY_SHAPE_PROXYTYPE;
		}
		else
		{
			PrintError("Unhandled BroadphaseNativeTypes string: %s\n", str.c_str());
			return MAX_BROADPHASE_COLLISION_TYPES;
		}
	}
} // namespace flex