#pragma once

class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btBroadphaseInterface;
class btSequentialImpulseConstraintSolver;

class btDiscreteDynamicsWorld;

namespace flex
{
	class PhysicsManager
	{
	public:
		void Initialize();
		void Destroy();

		btDiscreteDynamicsWorld* CreateWorld();

	private:
		bool m_bInitialized = false;

		btDefaultCollisionConfiguration * m_CollisionConfiguration = nullptr;
		btCollisionDispatcher* m_Dispatcher = nullptr;
		btBroadphaseInterface* m_OverlappingPairCache = nullptr;
		btSequentialImpulseConstraintSolver* m_Solver = nullptr;

		btDiscreteDynamicsWorld* m_PhysicsWorld = nullptr;

		//std::vector<btCollisionShape*> m_Shapes;

	};

} // namespace flex
