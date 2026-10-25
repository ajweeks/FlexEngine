
#include "stdafx.hpp"

#include "Graphics/DebugRenderer.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionObject.h>
#include <BulletCollision/CollisionShapes/btConcaveShape.h>
#include <BulletCollision/CollisionShapes/btTriangleCallback.h>
#include <BulletDynamics/ConstraintSolver/btTypedConstraint.h>
#include <BulletDynamics/Dynamics/btActionInterface.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <glm/gtx/quaternion.hpp> // for rotate
IGNORE_WARNINGS_POP

#include "Graphics/Renderer.hpp"
#include "Physics/DynamicsWorld.hpp"
#include "Physics/PhysicsDebuggingSettings.hpp"

namespace flex
{
	void DebugRenderer::flushLines()
	{
		Draw();
	}

	void DebugRenderer::DrawAxes(const btVector3& origin, const btQuaternion& orientation, real scale)
	{
		drawLine(origin, origin + quatRotate(orientation, btVector3(scale, 0.0f, 0.0f)), btVector3(0.9f, 0.1f, 0.1f));
		drawLine(origin, origin + quatRotate(orientation, btVector3(0.0f, scale, 0.0f)), btVector3(0.1f, 0.9f, 0.1f));
		drawLine(origin, origin + quatRotate(orientation, btVector3(0.0f, 0.0f, scale)), btVector3(0.1f, 0.1f, 0.9f));
	}

	void DebugRenderer::UpdateDebugMode()
	{
		const PhysicsDebuggingSettings& settings = g_Renderer->GetPhysicsDebuggingSettings();

		m_DebugMode =
			(settings.bDisableAll ? DBG_NoDebug : 0) |
			(settings.bDrawWireframe ? DBG_DrawWireframe : 0) |
			(settings.bDrawAabb ? DBG_DrawAabb : 0) |
			(settings.bDrawFeaturesText ? DBG_DrawFeaturesText : 0) |
			(settings.bDrawContactPoints ? DBG_DrawContactPoints : 0) |
			(settings.bNoDeactivation ? DBG_NoDeactivation : 0) |
			(settings.bNoHelpText ? DBG_NoHelpText : 0) |
			(settings.bDrawText ? DBG_DrawText : 0) |
			(settings.bProfileTimings ? DBG_ProfileTimings : 0) |
			(settings.bEnableSatComparison ? DBG_EnableSatComparison : 0) |
			(settings.bDisableBulletLCP ? DBG_DisableBulletLCP : 0) |
			(settings.bEnableCCD ? DBG_EnableCCD : 0) |
			(settings.bDrawConstraints ? DBG_DrawConstraints : 0) |
			(settings.bDrawConstraintLimits ? DBG_DrawConstraintLimits : 0) |
			(settings.bFastWireframe ? DBG_FastWireframe : 0) |
			(settings.bDrawNormals ? DBG_DrawNormals : 0) |
			(settings.bDrawFrames ? DBG_DrawFrames : 0);
	}

	void DebugRenderer::ClearLines()
	{
		m_LineSegmentIndex = 0;
	}

	namespace
	{
		real DistanceSqToAabb(const btVector3& point, const btVector3& aabbMin, const btVector3& aabbMax)
		{
			btVector3 closest = point;
			closest.setMax(aabbMin);
			closest.setMin(aabbMax);
			return (closest - point).length2();
		}

		// Draws triangle edges, skipping triangles entirely outside of the radius
		struct NearbyTriangleDrawCallback final : public btTriangleCallback
		{
			NearbyTriangleDrawCallback(btIDebugDraw* debugDrawer, const btTransform& worldTransform, const btVector3& localCenter, real radius, const btVector3& colour) :
				debugDrawer(debugDrawer),
				worldTransform(worldTransform),
				localCenter(localCenter),
				radiusSq(radius * radius),
				colour(colour)
			{
			}

			virtual void processTriangle(btVector3* triangle, int partId, int triangleIndex) override
			{
				FLEX_UNUSED(partId);
				FLEX_UNUSED(triangleIndex);

				if ((triangle[0] - localCenter).length2() > radiusSq &&
					(triangle[1] - localCenter).length2() > radiusSq &&
					(triangle[2] - localCenter).length2() > radiusSq)
				{
					return;
				}

				const btVector3 a = worldTransform * triangle[0];
				const btVector3 b = worldTransform * triangle[1];
				const btVector3 c = worldTransform * triangle[2];
				debugDrawer->drawLine(a, b, colour);
				debugDrawer->drawLine(b, c, colour);
				debugDrawer->drawLine(c, a, colour);
			}

			btIDebugDraw* debugDrawer;
			btTransform worldTransform;
			btVector3 localCenter;
			real radiusSq;
			btVector3 colour;
		};
	} // anonymous namespace

	void DebugRenderer::DrawPhysicsWorld(btDiscreteDynamicsWorld* world, const glm::vec3& center, real radius)
	{
		if (m_DebugMode == 0 || (m_DebugMode & DBG_NoDebug) != 0)
		{
			flushLines();
			return;
		}

		const btVector3 btCenter = ToBtVec3(center);
		const real radiusSq = radius * radius;
		const DefaultColors defaultColours = getDefaultColors();

		if ((m_DebugMode & DBG_DrawContactPoints) != 0 && world->getDispatcher() != nullptr)
		{
			btDispatcher* dispatcher = world->getDispatcher();
			const i32 manifoldCount = dispatcher->getNumManifolds();
			for (i32 i = 0; i < manifoldCount; ++i)
			{
				btPersistentManifold* manifold = dispatcher->getManifoldByIndexInternal(i);
				for (i32 j = 0; j < manifold->getNumContacts(); ++j)
				{
					const btManifoldPoint& point = manifold->getContactPoint(j);
					if ((point.m_positionWorldOnB - btCenter).length2() <= radiusSq)
					{
						drawContactPoint(point.m_positionWorldOnB, point.m_normalWorldOnB, point.getDistance(), point.getLifeTime(), defaultColours.m_contactPoint);
					}
				}
			}
		}

		if ((m_DebugMode & (DBG_DrawWireframe | DBG_DrawAabb)) != 0)
		{
			const btCollisionObjectArray& objects = world->getCollisionObjectArray();
			for (i32 i = 0; i < objects.size(); ++i)
			{
				const btCollisionObject* object = objects[i];
				if ((object->getCollisionFlags() & btCollisionObject::CF_DISABLE_VISUALIZE_OBJECT) != 0)
				{
					continue;
				}

				const btCollisionShape* shape = object->getCollisionShape();
				const btTransform& transform = object->getWorldTransform();
				btVector3 aabbMin, aabbMax;
				shape->getAabb(transform, aabbMin, aabbMax);
				if (DistanceSqToAabb(btCenter, aabbMin, aabbMax) > radiusSq)
				{
					continue;
				}

				if ((m_DebugMode & DBG_DrawWireframe) != 0)
				{
					btVector3 colour;
					switch (object->getActivationState())
					{
					case ACTIVE_TAG: colour = defaultColours.m_activeObject; break;
					case ISLAND_SLEEPING: colour = defaultColours.m_deactivatedObject; break;
					case WANTS_DEACTIVATION: colour = defaultColours.m_wantsDeactivationObject; break;
					case DISABLE_DEACTIVATION: colour = defaultColours.m_disabledDeactivationObject; break;
					case DISABLE_SIMULATION: colour = defaultColours.m_disabledSimulationObject; break;
					default: colour = btVector3(0.3f, 0.3f, 0.3f); break;
					}
					object->getCustomDebugColor(colour);

					if (shape->isConcave())
					{
						// Only visit triangles overlapping the radius (in the shape's local space)
						const btVector3 localCenter = transform.invXform(btCenter);
						const btVector3 extent(radius, radius, radius);
						NearbyTriangleDrawCallback callback(this, transform, localCenter, radius, colour);
						((const btConcaveShape*)shape)->processAllTriangles(&callback, localCenter - extent, localCenter + extent);
					}
					else
					{
						world->debugDrawObject(transform, shape, colour);
					}
				}

				if ((m_DebugMode & DBG_DrawAabb) != 0)
				{
					drawAabb(aabbMin, aabbMax, defaultColours.m_aabb);
				}
			}
		}

		if ((m_DebugMode & (DBG_DrawConstraints | DBG_DrawConstraintLimits)) != 0)
		{
			for (i32 i = world->getNumConstraints() - 1; i >= 0; --i)
			{
				btTypedConstraint* constraint = world->getConstraint(i);
				const btVector3 pos = constraint->getRigidBodyA().getWorldTransform().getOrigin();
				if ((pos - btCenter).length2() <= radiusSq)
				{
					world->debugDrawConstraint(constraint);
				}
			}
		}

		if ((m_DebugMode & (DBG_DrawWireframe | DBG_DrawAabb | DBG_DrawNormals)) != 0)
		{
			// Vehicles etc. (cheap, so not filtered)
			DynamicsWorld* dynamicsWorld = (DynamicsWorld*)world;
			for (i32 i = 0; i < dynamicsWorld->GetActionCount(); ++i)
			{
				dynamicsWorld->GetAction(i)->debugDraw(this);
			}
		}

		flushLines();
	}
} // namespace flex
