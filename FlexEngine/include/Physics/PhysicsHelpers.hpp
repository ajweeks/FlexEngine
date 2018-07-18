#pragma once

#pragma warning(push, 0)
#include <BulletCollision/BroadphaseCollision/btBroadphaseProxy.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>
#pragma warning(pop)

class btBoxShape;
class btSphereShape;
class btCapsuleShape;
class btCollisionShape;

namespace flex
{
	std::string CollisionShapeTypeToString(int shapeType);
	BroadphaseNativeTypes StringToCollisionShapeType(const std::string& str);

	enum class CollisionType
	{
		NOTHING = 0,
		DEFAULT = 1 << 0,
		EDITOR_OBJECT = 1 << 1
	};

	enum class PhysicsFlag : u32
	{
		TRIGGER =		(1 << 0),
		UNSELECTABLE =	(1 << 1), // Objects which can't be selected in the editor

		MAX_FLAG = (1 << 30)
	};

} // namespace flex
