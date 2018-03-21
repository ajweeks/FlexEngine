#pragma once

#pragma warning(push, 0)
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
class btCollisionShape;

namespace flex
{
	class PhysicsManager
	{
	public:
		void Initialize();
		void Destroy();

		btDiscreteDynamicsWorld* CreateWorld();
		btBoxShape* CreateBoxShape(const glm::vec3& halfExtent);

	private:
		bool m_Initialized = false;

		btDefaultCollisionConfiguration * m_CollisionConfiguration = nullptr;
		btCollisionDispatcher* m_Dispatcher = nullptr;
		btBroadphaseInterface* m_OverlappingPairCache = nullptr;
		btSequentialImpulseConstraintSolver* m_Solver = nullptr;

		btDiscreteDynamicsWorld* m_PhysicsWorld = nullptr;

		std::vector<btCollisionShape*> m_Shapes;

	};

	btVector3 ToBtVec3(const glm::vec3& rhs);
	btVector4 ToBtVec4(const glm::vec4& rhs);
	btQuaternion ToBtQuaternion(const glm::quat& rhs);

	glm::vec3 FromBtVec3(const btVector3& rhs);
	glm::vec4 FromBtVec4(const btVector4& rhs);
	glm::quat FromBtQuaternion(const btQuaternion& rhs);

} // namespace flex
