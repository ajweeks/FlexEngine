#include "stdafx.hpp"

#include "Physics/PhysicsWorld.hpp"

#pragma warning(push, 0)
#include <LinearMath/btVector3.h>
#include <BulletCollision/CollisionDispatch/btCollisionObject.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletCollision/NarrowPhaseCollision/btRaycastCallback.h>
#pragma warning(pop)

#include "Cameras/CameraManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Physics/PhysicsManager.hpp"
#include "Physics/PhysicsHelpers.hpp"
#include "Physics/RigidBody.hpp"
#include "Scene/GameObject.hpp"
#include "Helpers.hpp"

namespace flex
{
	PhysicsWorld::PhysicsWorld()
	{
	}

	PhysicsWorld::~PhysicsWorld()
	{
	}

	void PhysicsWorld::Initialize()
	{
		if (!m_World)
		{
			m_World = g_PhysicsManager->CreateWorld();
			
			m_World->setInternalTickCallback(PhysicsInternalTickCallback, this);

			//m_World->getPairCache()->setInternalGhostPairCallback()
			//m_World->getDebugDrawer()->setDebugMode(btIDebugDraw::DBG_DrawAabb);
		}
	}

	void PhysicsWorld::Destroy()
	{
		if (m_World)
		{
			for (i32 i = m_World->getNumCollisionObjects() - 1; i >= 0; --i)
			{
				btCollisionObject* obj = m_World->getCollisionObjectArray()[i];
				btRigidBody* body = btRigidBody::upcast(obj);
				if (body && body->getMotionState())
				{
					delete body->getMotionState();
				}
				m_World->removeCollisionObject(obj);
				delete obj;
			}
			
			SafeDelete(m_World);
		}
	}

	void PhysicsWorld::Update(sec deltaSeconds)
	{
		if (m_World)
		{
			m_World->stepSimulation(deltaSeconds, MAX_SUBSTEPS);
		}
	}

	btDiscreteDynamicsWorld* PhysicsWorld::GetWorld()
	{
		return m_World;
	}

	btVector3 PhysicsWorld::GenerateDirectionRayFromScreenPos(i32 x, i32 y)
	{
		real frameBufferWidth = (real)g_Window->GetFrameBufferSize().x;
		real frameBufferHeight = (real)g_Window->GetFrameBufferSize().y;
		btScalar aspectRatio = frameBufferWidth / frameBufferHeight;

		BaseCamera* camera = g_CameraManager->CurrentCamera();
		real fov = camera->GetFOV();
		real tanFov = tanf(0.5f * fov);

		real pixelScreenX = 2.0f * ((x + 0.5f) / frameBufferWidth) - 1.0f;
		real pixelScreenY = 1.0f - 2.0f * ((y + 0.5f) / frameBufferHeight);

		real pixelCameraX = pixelScreenX * aspectRatio * tanFov;
		real pixelCameraY = pixelScreenY * tanFov;


		glm::mat4 cameraView = glm::inverse(camera->GetView());

		glm::vec4 rayOrigin(0, 0, 0, 1);
		glm::vec3 rayOriginWorld = cameraView * rayOrigin;

		glm::vec3 rayPWorld = cameraView * glm::vec4(pixelCameraX, pixelCameraY, -1.0f, 1);
		btVector3 rayDirection = Vec3ToBtVec3(rayPWorld - rayOriginWorld);
		rayDirection.normalize();

		return rayDirection;
	}

	btRigidBody* PhysicsWorld::PickBody(const btVector3& rayStart, const btVector3& rayEnd)
	{
		btRigidBody* pickedBody = nullptr;
		//btVector3 hitPos(0.0f, 0.0f, 0.0f);
		//real pickingDist = 0.0f;
		//i32 savedState = 0;
		//btTypedConstraint* pickedConstraint = nullptr;

		btCollisionWorld::AllHitsRayResultCallback rayCallback(rayStart, rayEnd);
		m_World->rayTest(rayStart, rayEnd, rayCallback);
		if (rayCallback.hasHit())
		{
			for (i32 i = 0; i < rayCallback.m_hitPointWorld.size(); ++i)
			{
				btVector3 pickPos = rayCallback.m_hitPointWorld[i];
				btRigidBody* body = (btRigidBody*)btRigidBody::upcast(rayCallback.m_collisionObject);
				if (body)
				{
					GameObject* pickedGameObject = (GameObject*)body->getUserPointer();

					if (pickedGameObject)
					{
						pickedBody = body;
						break;
					}

					//pickedBody->activate(true);
					//pickedBody->clearForces();
					//
					//btVector3 localPivot = body->getCenterOfMassTransform().inverse() * pickPos;
					//pickedBody->applyForce({ 0, 600, 0 }, localPivot);

					//savedState = pickedBody->getActivationState();
					//pickedBody->setActivationState(DISABLE_DEACTIVATION);

					//btPoint2PointConstraint* p2p = new btPoint2PointConstraint(*body, localPivot);
					//dynamicsWorld->addConstraint(p2p, true);
					//pickedConstraint = p2p;
					//btScalar mousePickClamping = 30.f;
					//p2p->m_setting.m_impulseClamp = mousePickClamping;
					//p2p->m_setting.m_tau = 0.001f;
				}
			}
			//hitPos = pickPos;
			//pickingDist = (pickPos - rayFromWorld).length();
		}

		return pickedBody;
	}

	// CLEANUP:
	void PhysicsInternalTickCallback(btDynamicsWorld* world, btScalar timeStep)
	{
		UNREFERENCED_PARAMETER(timeStep);

		PhysicsWorld* physWorld = static_cast<PhysicsWorld*>(world->getWorldUserInfo());

		std::set<std::pair<const btCollisionObject*, const btCollisionObject*>> collisionPairsFoundThisStep;

		i32 numManifolds = world->getDispatcher()->getNumManifolds();
		for (i32 i = 0; i < numManifolds; ++i)
		{
			btPersistentManifold* contactManifold = world->getDispatcher()->getManifoldByIndexInternal(i);
			const btCollisionObject* obA = contactManifold->getBody0();
			const btCollisionObject* obB = contactManifold->getBody1();

			GameObject* obAGameObject = (GameObject*)obA->getUserPointer();
			GameObject* obBGameObject = (GameObject*)obB->getUserPointer();

			i32 numContacts = contactManifold->getNumContacts();

			if (numContacts > 0 && obAGameObject && obBGameObject)
			{
				u32 obAFlags = obAGameObject->GetRigidBody()->GetPhysicsFlags();
				u32 obBFlags = obBGameObject->GetRigidBody()->GetPhysicsFlags();

				i32 bObATrigger = (obAFlags & (u32)PhysicsFlag::TRIGGER);
				i32 bObBTrigger = (obBFlags & (u32)PhysicsFlag::TRIGGER);
				// If exactly one of the two objects is a trigger (not both)
				if (bObATrigger ^ bObBTrigger)
				{
					const btCollisionObject* triggerCollisionObject = (bObATrigger ? obA : obB);
					const btCollisionObject* otherCollisionObject = (bObATrigger ? obB: obA);
					GameObject* trigger = (bObATrigger ? obAGameObject : obBGameObject);
					GameObject* other = (bObATrigger ? obBGameObject : obAGameObject);

					if (!other->IsStatic())
					{
						std::pair<const btCollisionObject*, const btCollisionObject*> pair(triggerCollisionObject, otherCollisionObject);
						collisionPairsFoundThisStep.insert(pair);

						if (physWorld->m_CollisionPairs.find(pair) == physWorld->m_CollisionPairs.end())
						{
							trigger->OnOverlapBegin(other);
							other->OnOverlapBegin(trigger);
							//Print("Trigger collision begin " + obAGameObject->GetName() + " : " + obBGameObject->GetName());
						}
					}
				}
			}

			//for (i32 j = 0; j < numContacts; ++j)
			//{
			//	btManifoldPoint& pt = contactManifold->getContactPoint(j);
			//	if (pt.getDistance() < 0.0f)
			//	{
			//		const btVector3& ptA = pt.getPositionWorldOnA();
			//		const btVector3& ptB = pt.getPositionWorldOnB();
			//		const btVector3& normalOnB = pt.m_normalWorldOnB;
			//		//Print("contact: " + std::to_string(normalOnB.getX()) + ", " + 
			//		//				std::to_string(normalOnB.getY()) + ", " + std::to_string(normalOnB.getZ()));
			//	}
			//}
		}

		std::set<std::pair<const btCollisionObject*, const btCollisionObject*>> differentPairs;
		std::set_difference(physWorld->m_CollisionPairs.begin(), physWorld->m_CollisionPairs.end(),
							collisionPairsFoundThisStep.begin(), collisionPairsFoundThisStep.end(),
							std::inserter(differentPairs, differentPairs.begin()));

		for (const auto& pair : differentPairs)
		{
			GameObject* triggerGameObject = (GameObject*)pair.first->getUserPointer();
			GameObject* otherGameObject = (GameObject*)pair.second->getUserPointer();
			//Print("Trigger collision end " + triggerGameObject->GetName() + " : " + otherGameObject->GetName());
			triggerGameObject->OnOverlapEnd(otherGameObject);
			otherGameObject->OnOverlapEnd(triggerGameObject);
		}

		physWorld->m_CollisionPairs.clear();
		for (const auto& pair : collisionPairsFoundThisStep)
		{
			physWorld->m_CollisionPairs.insert(pair);
		}
	}
} // namespace flex

