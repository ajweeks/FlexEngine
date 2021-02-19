#pragma once

IGNORE_WARNINGS_PUSH
#include <BulletCollision/BroadphaseCollision/btBroadphaseProxy.h>
IGNORE_WARNINGS_POP

namespace flex
{
	static BroadphaseNativeTypes g_CollisionTypes[] =
	{
		BOX_SHAPE_PROXYTYPE,
		TRIANGLE_SHAPE_PROXYTYPE,
		TETRAHEDRAL_SHAPE_PROXYTYPE,
		CONVEX_TRIANGLEMESH_SHAPE_PROXYTYPE,
		SPHERE_SHAPE_PROXYTYPE,
		MULTI_SPHERE_SHAPE_PROXYTYPE,
		CAPSULE_SHAPE_PROXYTYPE,
		CONE_SHAPE_PROXYTYPE,
		CYLINDER_SHAPE_PROXYTYPE,
		TRIANGLE_MESH_SHAPE_PROXYTYPE,
		TERRAIN_SHAPE_PROXYTYPE,
		STATIC_PLANE_PROXYTYPE,
		SOFTBODY_SHAPE_PROXYTYPE,

		MAX_BROADPHASE_COLLISION_TYPES
	};

	static const char* g_CollisionTypeStrs[] =
	{
		"box",
		"triangle",
		"tetrahedral",
		"convex triangle mesh",
		"sphere",
		"multi sphere",
		"capsule",
		"cone",
		"cylinder",
		"triangle mesh",
		"terrain",
		"static plane",
		"soft body",

		"NONE"
	};

	static_assert(ARRAY_LENGTH(g_CollisionTypeStrs) == ARRAY_LENGTH(g_CollisionTypes), "g_CollisionTypeStrs length must match g_CollisionTypes length");

	std::string CollisionShapeTypeToString(int shapeType);
	BroadphaseNativeTypes StringToCollisionShapeType(const std::string& str);

	enum class CollisionType
	{
		NOTHING = 0,
		DEFAULT = 1 << 0,
		EDITOR_OBJECT = 1 << 1,
		STATIC = 1 << 2,
		EVERYTHING = ~0
	};

	enum class PhysicsFlag : u32
	{
		TRIGGER =		(1 << 0),
		UNSELECTABLE =	(1 << 1), // Objects which can't be selected in the editor

		MAX_FLAG = (1 << 30)
	};

} // namespace flex
