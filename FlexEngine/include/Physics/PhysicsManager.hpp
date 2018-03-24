#pragma once

#pragma warning(push, 0)
// TODO: Include only required headers rather than common headers
#include <btBulletDynamicsCommon.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>
#pragma warning(pop)

#include <vector>


class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btBroadphaseInterface;
class btSequentialImpulseConstraintSolver;

class btDiscreteDynamicsWorld;

class btBoxShape;
class btSphereShape;
class btCapsuleShape;
class btCollisionShape;

namespace flex
{
	class PhysicsManager
	{
	public:
		void Initialize();
		void Destroy();

		btDiscreteDynamicsWorld* CreateWorld();
		btBoxShape* CreateBoxShape(const btVector3& halfExtent);
		btSphereShape* CreateSphereShape(btScalar radius);
		btCapsuleShape* CreateCapsuleShape(btScalar radius, btScalar height);

	private:
		bool m_Initialized = false;

		btDefaultCollisionConfiguration * m_CollisionConfiguration = nullptr;
		btCollisionDispatcher* m_Dispatcher = nullptr;
		btBroadphaseInterface* m_OverlappingPairCache = nullptr;
		btSequentialImpulseConstraintSolver* m_Solver = nullptr;

		btDiscreteDynamicsWorld* m_PhysicsWorld = nullptr;

		std::vector<btCollisionShape*> m_Shapes;

	};

} // namespace flex
