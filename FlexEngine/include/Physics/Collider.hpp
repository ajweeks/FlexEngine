#pragma once


class btBoxShape;
class btSphereShape;
class btCapsuleShape;
class btCollisionShape;

namespace flex
{
	class Collider
	{
	public:
		Collider();
		virtual ~Collider();

		btBoxShape* CreateBoxCollider(const GameContext& gameContext, const btVector3& halfExtents);
		btSphereShape* CreateSphereCollider(const GameContext& gameContext, btScalar radius);
		btCapsuleShape* CreateCapsuleCollider(const GameContext& gameContext, btScalar radius, btScalar height);

		btCollisionShape* GetShape();
		btVector3 GetScale();

	private:
		btCollisionShape* m_Shape = nullptr;
		/* A uniform mesh should have this value as its scale to match the size of this collider */
		btVector3 m_Scale;

	};
} // namespace flex
