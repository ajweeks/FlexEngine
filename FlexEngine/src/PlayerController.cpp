#include "stdafx.hpp"

#include "PlayerController.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <LinearMath/btIDebugDraw.h>
IGNORE_WARNINGS_POP

#include <list>

#include "Audio/AudioManager.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/FirstPersonCamera.hpp"
#include "Cameras/OverheadCamera.hpp"
#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsTypeConversions.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/TrackManager.hpp"
#include "UIMesh.hpp"
#include "Window/Window.hpp"

namespace flex
{
	PlayerController::PlayerController() :
		m_ActionCallback(this, &PlayerController::OnActionEvent),
		m_MouseMovedCallback(this, &PlayerController::OnMouseMovedEvent)
	{
	}

	PlayerController::~PlayerController()
	{
	}

	void PlayerController::Initialize(Player* player)
	{
		m_Player = player;
		m_PlayerIndex = m_Player->GetIndex();

		assert(m_PlayerIndex == 0 ||
			m_PlayerIndex == 1);

		m_Player->UpdateIsPossessed();

		g_InputManager->BindActionCallback(&m_ActionCallback, 15);
		g_InputManager->BindMouseMovedCallback(&m_MouseMovedCallback, 15);
	}

	void PlayerController::Destroy()
	{
		g_InputManager->UnbindActionCallback(&m_ActionCallback);
		g_InputManager->UnbindMouseMovedCallback(&m_MouseMovedCallback);
	}

	void PlayerController::Update()
	{
		if (m_Player->m_bPossessed &&
			g_Window->HasFocus() &&
			!g_EngineInstance->IsSimulationPaused())
		{
			g_Window->SetCursorMode(m_Player->bInventoryShowing ? CursorMode::NORMAL : CursorMode::DISABLED);
		}

		// TODO: Make frame-rate-independent!

		btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();

		const real moveLR = -g_InputManager->GetActionAxisValue(Action::MOVE_LEFT) + g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);
		const real moveFB = -g_InputManager->GetActionAxisValue(Action::MOVE_BACKWARD) + g_InputManager->GetActionAxisValue(Action::MOVE_FORWARD);
		const real lookLR = -g_InputManager->GetActionAxisValue(Action::LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::LOOK_RIGHT);

		auto GetObjectPointedAt = [this]() -> GameObject*
		{
			PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();

			btVector3 rayStart, rayEnd;
			FlexEngine::GenerateRayAtScreenCenter(rayStart, rayEnd, m_ItemPickupMaxDist);

			const btRigidBody* pickedBody = physicsWorld->PickFirstBody(rayStart, rayEnd);

			if (pickedBody != nullptr)
			{
				GameObject* gameObject = static_cast<GameObject*>(pickedBody->getUserPointer());
				real dist = glm::distance(gameObject->GetTransform()->GetWorldPosition(), m_Player->m_Transform.GetWorldPosition());
				if (dist < m_ItemPickupMaxDist && gameObject->IsItemizable())
				{
					return gameObject;
				}
			}

			return nullptr;
		};

		if (m_Player->m_bPossessed)
		{
			real cycleItemAxis = g_InputManager->GetActionAxisValue(Action::CYCLE_HELD_ITEM_FORWARD);
			if (!ImGui::GetIO().WantCaptureMouse)
			{
				if (cycleItemAxis > 0.0f)
				{
					m_Player->heldItemSlot = (m_Player->heldItemSlot + 1) % Player::QUICK_ACCESS_ITEM_COUNT;
				}
				else if (cycleItemAxis < 0.0f)
				{
					m_Player->heldItemSlot = (m_Player->heldItemSlot - 1 + Player::QUICK_ACCESS_ITEM_COUNT) % Player::QUICK_ACCESS_ITEM_COUNT;
				}
			}

			if (m_Player->m_TrackRidingID == InvalidTrackID)
			{
				TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

				if (m_Player->m_bPlacingTrack)
				{
					glm::vec3 reticlePos = m_Player->GetTrackPlacementReticlePosWS(1.0f);

					if (m_bAttemptInteract)
					{
						m_bAttemptInteract = false;

						// Place new node
						m_Player->m_CurvePlacing.points[m_Player->m_CurveNodesPlaced++] = reticlePos;
						if (m_Player->m_CurveNodesPlaced == 4)
						{
							AudioManager::PlaySource(m_Player->m_SoundPlaceFinalTrackNodeID);

							m_Player->m_CurvePlacing.CalculateLength();
							m_Player->m_TrackPlacing.curves.push_back(m_Player->m_CurvePlacing);

							glm::vec3 prevHandlePos = m_Player->m_CurvePlacing.points[2];

							m_Player->m_CurveNodesPlaced = 0;
							m_Player->m_CurvePlacing.points[0] = m_Player->m_CurvePlacing.points[1] = m_Player->m_CurvePlacing.points[2] = m_Player->m_CurvePlacing.points[3] = VEC3_ZERO;

							glm::vec3 controlPointPos = reticlePos;
							glm::vec3 nextHandlePos = controlPointPos + (controlPointPos - prevHandlePos);
							m_Player->m_CurvePlacing.points[m_Player->m_CurveNodesPlaced++] = controlPointPos;
							m_Player->m_CurvePlacing.points[m_Player->m_CurveNodesPlaced++] = nextHandlePos;
						}
						else
						{
							AudioManager::PlaySource(m_Player->m_SoundPlaceTrackNodeID);
						}

						for (i32 i = 3; i > m_Player->m_CurveNodesPlaced - 1; --i)
						{
							m_Player->m_CurvePlacing.points[i] = reticlePos;
						}
					}

					if (m_bAttemptCompleteTrack)
					{
						m_bAttemptCompleteTrack = false;

						m_Player->m_CurveNodesPlaced = 0;
						m_Player->m_CurvePlacing.points[0] = m_Player->m_CurvePlacing.points[1] = m_Player->m_CurvePlacing.points[2] = m_Player->m_CurvePlacing.points[3] = VEC3_ZERO;

						if (!m_Player->m_TrackPlacing.curves.empty())
						{
							trackManager->AddTrack(m_Player->m_TrackPlacing);
							trackManager->FindJunctions();
							m_Player->m_TrackPlacing.curves.clear();
						}
					}

					static const btVector4 placedCurveCol(0.5f, 0.8f, 0.3f, 0.9f);
					static const btVector4 placingCurveCol(0.35f, 0.6f, 0.3f, 0.9f);
					for (const BezierCurve3D& curve : m_Player->m_TrackPlacing.curves)
					{
						curve.DrawDebug(false, placedCurveCol, placedCurveCol);

					}
					if (m_Player->m_CurveNodesPlaced > 0)
					{
						m_Player->m_CurvePlacing.DrawDebug(false, placingCurveCol, placingCurveCol);
					}

					btTransform cylinderTransform(ToBtQuaternion(m_Player->m_Transform.GetWorldRotation()), ToBtVec3(reticlePos));
					debugDrawer->drawCylinder(0.6f, 0.001f, 1, cylinderTransform, btVector3(0.18f, 0.22f, 0.35f));
					debugDrawer->drawCylinder(1.1f, 0.001f, 1, cylinderTransform, btVector3(0.18f, 0.22f, 0.35f));
				}

				if (m_Player->m_bEditingTrack)
				{
					// TODO: Snap to points other than the one we are editing
					glm::vec3 reticlePos = m_Player->GetTrackPlacementReticlePosWS(m_Player->m_TrackEditingID == InvalidTrackID ? 1.0f : 0.0f, true);
					if (m_bAttemptInteract)
					{
						m_bAttemptInteract = false;

						if (m_Player->m_TrackEditingCurveIdx == -1)
						{
							// Select node
							TrackID trackID = InvalidTrackID;
							i32 curveIndex = -1;
							i32 pointIndex = -1;
							if (trackManager->GetPointInRange(reticlePos, 0.1f, &trackID, &curveIndex, &pointIndex))
							{
								m_Player->m_TrackEditingID = trackID;
								m_Player->m_TrackEditingCurveIdx = curveIndex;
								m_Player->m_TrackEditingPointIdx = pointIndex;
							}
						}
						else
						{
							// Select none
							m_Player->m_TrackEditingID = InvalidTrackID;
							m_Player->m_TrackEditingCurveIdx = -1;
							m_Player->m_TrackEditingPointIdx = -1;
						}
					}

					if (m_Player->m_TrackEditingID != InvalidTrackID)
					{
						BezierCurveList* trackEditing = trackManager->GetTrack(m_Player->m_TrackEditingID);
						glm::vec3 point = trackEditing->GetPointOnCurve(m_Player->m_TrackEditingCurveIdx, m_Player->m_TrackEditingPointIdx);

						glm::vec3 newPoint(reticlePos.x, point.y, reticlePos.z);
						trackEditing->SetPointPosAtIndex(m_Player->m_TrackEditingCurveIdx, m_Player->m_TrackEditingPointIdx, newPoint, true);

						static const btVector4 editedCurveCol(0.3f, 0.85f, 0.53f, 0.9f);
						static const btVector4 editingCurveCol(0.2f, 0.8f, 0.25f, 0.9f);
						for (const BezierCurve3D& curve : trackEditing->curves)
						{
							curve.DrawDebug(false, editedCurveCol, editingCurveCol);
						}

						trackManager->FindJunctions();
					}

					btTransform cylinderTransform(ToBtQuaternion(m_Player->m_Transform.GetWorldRotation()), ToBtVec3(reticlePos));
					static btVector3 ringColEditing(0.48f, 0.22f, 0.65f);
					static btVector3 ringColEditingActive(0.6f, 0.4f, 0.85f);
					debugDrawer->drawCylinder(0.6f, 0.001f, 1, cylinderTransform, m_Player->m_TrackEditingID == InvalidTrackID ? ringColEditing : ringColEditingActive);
					debugDrawer->drawCylinder(1.1f, 0.001f, 1, cylinderTransform, m_Player->m_TrackEditingID == InvalidTrackID ? ringColEditing : ringColEditingActive);
				}

				if (m_bAttemptPickup)
				{
					m_bAttemptPickup = false;

					GameObject* pickedItem = GetObjectPointedAt();
					if (pickedItem != nullptr)
					{
						m_ItemPickingUp = pickedItem;
						m_ItemPickingTimer = m_ItemPickingDuration;
					}
				}
			}

			if (m_ItemPickingTimer != -1.0f)
			{
				m_ItemPickingTimer -= g_DeltaTime;

				if (m_ItemPickingTimer <= 0.0f)
				{
					m_ItemPickingTimer = -1.0f;

					m_Player->AddToInventory(m_ItemPickingUp->Itemize(), 1);
				}
				else
				{
					GameObject* pickedItem = GetObjectPointedAt();
					if (pickedItem == nullptr || pickedItem != m_ItemPickingUp)
					{
						m_ItemPickingUp = nullptr;
						m_ItemPickingTimer = -1.0f;
					}
					else
					{
						real startAngle = PI_DIV_TWO - (1.0f - m_ItemPickingTimer / m_ItemPickingDuration) * TWO_PI;
						real endAngle = PI_DIV_TWO;
						g_Renderer->GetUIMesh()->DrawArc(VEC2_ZERO, startAngle, endAngle, 0.05f, 0.025f, 32, VEC4_ONE);
					}
				}
			}

#if 0 // Arc & rect tests
			real startAngle = PI_DIV_TWO;
			real endAngle = PI_DIV_TWO - TWO_PI - 0.1f;
			g_Renderer->GetUIMesh()->DrawArc(VEC2_ZERO, startAngle, endAngle, 0.05f, 0.025f, 32, VEC4_ONE);

			startAngle = PI - 0.1f;
			endAngle = PI + PI_DIV_TWO;
			g_Renderer->GetUIMesh()->DrawArc(glm::vec2(0.2f, 0.0f), startAngle, endAngle, 0.05f, 0.025f, 32, VEC4_ONE);

			startAngle = -PI_DIV_TWO;
			endAngle = 0.1f;
			g_Renderer->GetUIMesh()->DrawArc(glm::vec2(0.2f, 0.0f), startAngle, endAngle, 0.05f, 0.03f, 32, glm::vec4(0.9f, 0.92f, 0.97f, 1.0f));

			startAngle = (sin(g_SecElapsedSinceProgramStart) * 0.5f + 0.5f) * TWO_PI;
			endAngle = startAngle + (sin(g_SecElapsedSinceProgramStart * 0.5f + 1.0f) * 0.5f + 0.51f) * TWO_PI;
			g_Renderer->GetUIMesh()->DrawArc(glm::vec2(0.0f, -0.2f), startAngle, endAngle, 0.05f, 0.025f, 32, VEC4_ONE);

			startAngle = 0.0f;
			endAngle = TWO_PI;
			g_Renderer->GetUIMesh()->DrawArc(glm::vec2(0.2f, 0.2f), startAngle, endAngle, 0.07f, 0.035f, 32, VEC4_ONE);

			glm::vec2 rectPos(sin(g_SecElapsedSinceProgramStart) * 0.05f - 0.3f, 0.0f);
			glm::vec2 halfRectSize(sin(g_SecElapsedSinceProgramStart * 0.5f) * 0.01f + 0.2f, cos(g_SecElapsedSinceProgramStart * 0.5f) * 0.01f + 0.2f);
			g_Renderer->GetUIMesh()->DrawRect(rectPos - halfRectSize, rectPos + halfRectSize, VEC4_ONE, 0.0f);
#endif

			// TODO: Allow player to (dis)connect pluggables together

			GameObject* nearestInteractable = nullptr;
			if (m_Player->m_ObjectInteractingWith == nullptr)
			{
				std::vector<GameObject*> allInteractableObjects;
				g_SceneManager->CurrentScene()->GetInteractableObjects(allInteractableObjects);

				if (!allInteractableObjects.empty())
				{
					// List of objects sorted by sqr dist (closest to farthest)
					std::list<Pair<GameObject*, real>> nearbyInteractables;

					// TODO: Move to All.variables
					real maxDistSqr = 7.0f * 7.0f;
					const glm::vec3 pos = m_Player->m_Transform.GetWorldPosition();
					for (GameObject* obj : allInteractableObjects)
					{
						if (obj != m_Player)
						{
							real dist2 = glm::distance2(obj->GetTransform()->GetWorldPosition(), pos);
							if (dist2 <= maxDistSqr)
							{
								// Insert into sorted list
								bool bInserted = false;
								for (auto iter = nearbyInteractables.begin(); iter != nearbyInteractables.end(); ++iter)
								{
									if (iter->second > dist2)
									{
										nearbyInteractables.insert(iter, { obj, dist2 });
										bInserted = true;
										break;
									}
								}
								if (!bInserted)
								{
									nearbyInteractables.emplace_back(obj, dist2);
								}
							}
						}
					}

					for (auto iter = nearbyInteractables.begin(); iter != nearbyInteractables.end(); ++iter)
					{
						GameObject* nearbyInteractable = iter->first;
						if (!nearbyInteractable->IsBeingInteractedWith())
						{
							if (nearbyInteractable->AllowInteractionWith(m_Player))
							{
								nearestInteractable = nearbyInteractable;
								nearestInteractable->SetNearbyInteractable(m_Player);
								break;
							}
						}
					}
				}
			}

			if (m_bAttemptInteract)
			{
				if (m_Player->m_ObjectInteractingWith)
				{
					// Toggle interaction when already interacting
					m_Player->m_ObjectInteractingWith->SetInteractingWith(nullptr);
					m_Player->SetInteractingWith(nullptr);
					m_bAttemptInteract = false;
				}
				else
				{
					if (nearestInteractable != nullptr)
					{
						nearestInteractable->SetInteractingWith(m_Player);
						m_Player->SetInteractingWith(nearestInteractable);
						m_bAttemptInteract = false;
					}
					//else if (m_Player->m_HeldItem != nullptr)
					//{
					//	if (m_Player->m_HeldItem->GetTypeID() == SID("wire"))
					//	{
					//		Wire* wire = (Wire*)m_Player->m_HeldItem;
					//		wire->SetInteractingWith(nullptr);
					//	}
					//	m_Player->m_HeldItem = nullptr;
					//}
				}
			}
		}

		if (m_bPreviewPlaceItemFromInventory)
		{
			if (m_Player->heldItemSlot == -1)
			{
				m_Player->heldItemSlot = 0;
			}

			GameObjectStack& gameObjectStack = m_Player->m_QuickAccessInventory[m_Player->heldItemSlot];

			if (gameObjectStack.count >= 1)
			{
				if (gameObjectStack.prefabID.IsValid())
				{
					// TODO: LERP
					Transform* playerTransform = m_Player->GetTransform();
					m_TargetItemPlacementPos = playerTransform->GetWorldPosition() +
						playerTransform->GetForward() * 4.0f;
					m_TargetItemPlacementRot = playerTransform->GetWorldRotation();

					g_Renderer->QueueHologramMesh(gameObjectStack.prefabID,
						m_TargetItemPlacementPos,
						m_TargetItemPlacementRot,
						VEC3_ONE);
				}
				else
				{
					std::string prefabIDStr = gameObjectStack.prefabID.ToString();
					PrintError("Failed to de-itemize item from player inventory, invalid prefab ID: %s\n", prefabIDStr.c_str());
				}
			}
		}

		if (m_bAttemptPlaceItemFromInventory)
		{
			m_bAttemptPlaceItemFromInventory = false;

			if (m_Player->heldItemSlot == -1)
			{
				m_Player->heldItemSlot = 0;
			}

			GameObjectStack& gameObjectStack = m_Player->m_QuickAccessInventory[m_Player->heldItemSlot];

			if (gameObjectStack.count >= 1)
			{
				if (gameObjectStack.prefabID.IsValid())
				{
					GameObject* gameObject = GameObject::Deitemize(gameObjectStack.prefabID, m_TargetItemPlacementPos, m_TargetItemPlacementRot);
					if (gameObject != nullptr)
					{
						// Add non-immediate
						g_SceneManager->CurrentScene()->AddRootObject(gameObject);
						gameObjectStack.count--;

						if (gameObjectStack.count == 0)
						{
							gameObjectStack.prefabID = InvalidPrefabID;
						}
					}
					else
					{
						std::string prefabIDStr = gameObjectStack.prefabID.ToString();
						PrintError("Failed to de-itemize item with prefab ID %s from player inventory\n", prefabIDStr.c_str());
					}
				}
				else
				{
					std::string prefabIDStr = gameObjectStack.prefabID.ToString();
					PrintError("Failed to de-itemize item from player inventory, invalid prefab ID: %s\n", prefabIDStr.c_str());
				}
			}
		}

		m_Player->GetRigidBody()->UpdateParentTransform();

		btRigidBody* rb = m_Player->GetRigidBody()->GetRigidBodyInternal();
		btVector3 rbPos = rb->getInterpolationWorldTransform().getOrigin();

		Transform* transform = m_Player->GetTransform();
		glm::vec3 up = transform->GetUp();
		glm::vec3 right = transform->GetRight();
		glm::vec3 forward = transform->GetForward();
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
		TrackID pTrackRidingID = m_Player->m_TrackRidingID;
		bool bWasFacingDownTrack = m_Player->IsFacingDownTrack();

		if (m_Player->m_bPossessed)
		{
			if (g_InputManager->IsGamepadButtonDown(m_PlayerIndex, GamepadButton::BACK))
			{
				ResetTransformAndVelocities();
				return;
			}

			if (m_bAttemptInteract)
			{
				if (m_Player->m_TrackRidingID == InvalidTrackID)
				{
					real distAlongTrack = -1.0f;
					TrackID trackInRangeIndex = trackManager->GetTrackInRangeID(transform->GetWorldPosition(), m_Player->m_TrackAttachMinDist, &distAlongTrack);
					if (trackInRangeIndex != InvalidTrackID)
					{
						m_bAttemptInteract = false;

						m_Player->AttachToTrack(trackInRangeIndex, distAlongTrack);

						SnapPosToTrack(m_Player->m_DistAlongTrack, false);
					}
				}
				else
				{
					m_bAttemptInteract = false;
					m_Player->DetachFromTrack();
				}
			}
		}

		btVector3 force(0.0f, 0.0f, 0.0f);

		m_Player->UpdateIsGrounded();

		bool bInteractingWithTerminal = false;
		bool bInteractingWithVehicle = false;
		{
			GameObject* objInteractingWith = m_Player->GetObjectInteractingWith();
			bInteractingWithTerminal = (objInteractingWith != nullptr) && (objInteractingWith->GetTypeID() == SID("terminal"));
			bInteractingWithVehicle = (objInteractingWith != nullptr) && (objInteractingWith->GetTypeID() == SID("vehicle"));
		}

		if (m_Player->m_bPossessed)
		{
			if (m_Player->m_TrackRidingID != InvalidTrackID)
			{

				glm::vec3 newCurveDir = trackManager->GetTrack(m_Player->m_TrackRidingID)->GetCurveDirectionAt(m_Player->m_DistAlongTrack);
				static glm::vec3 pCurveDir = newCurveDir;

				real move = moveFB;

				const bool bReversing = (move < 0.0f);

				if (!m_Player->IsFacingDownTrack())
				{
					move = -move;
				}

				real targetDDist = move * m_Player->m_TrackMoveSpeed * g_DeltaTime;
				real pDist = m_Player->m_DistAlongTrack;
				m_Player->m_DistAlongTrack = trackManager->AdvanceTAlongTrack(m_Player->m_TrackRidingID,
					targetDDist, m_Player->m_DistAlongTrack);
				SnapPosToTrack(pDist, bReversing);

				if (m_Player->IsFacingDownTrack() != bWasFacingDownTrack &&
					m_Player->m_TrackRidingID == pTrackRidingID)
				{
					AudioManager::PlaySource(m_Player->m_SoundTrackSwitchDirID);
				}

				m_Player->m_pDTrackMovement = m_Player->m_DistAlongTrack - pDist;
				pCurveDir = newCurveDir;
			}
			else
			{
				if (!bInteractingWithTerminal && !bInteractingWithVehicle)
				{
					force += ToBtVec3(transform->GetRight()) * m_MoveAcceleration * moveLR;
					force += ToBtVec3(transform->GetForward()) * m_MoveAcceleration * moveFB;
				}
			}
		}

		bool bDrawLocalAxes = (m_Mode != Mode::FIRST_PERSON) && m_Player->IsVisible();
		if (bDrawLocalAxes)
		{
			const real lineLength = 4.0f;
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(up) * lineLength, btVector3(0.0f, 1.0f, 0.0f));
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(forward) * lineLength, btVector3(0.0f, 0.0f, 1.0f));
			debugDrawer->drawLine(rbPos, rbPos + ToBtVec3(right) * lineLength, btVector3(1.0f, 0.0f, 0.0f));
		}

		rb->applyCentralForce(force);

		const btVector3& vel = rb->getLinearVelocity();
		btVector3 xzVel(vel.getX(), 0, vel.getZ());
		real xzVelMagnitude = xzVel.length();
		bool bMaxVel = false;
		if (xzVelMagnitude > m_MaxMoveSpeed)
		{
			btVector3 xzVelNorm = xzVel.normalized();
			btVector3 newVel(xzVelNorm.getX() * m_MaxMoveSpeed, vel.getY(), xzVelNorm.getZ() * m_MaxMoveSpeed);
			rb->setLinearVelocity(newVel);
			bMaxVel = true;
		}

		// Jitter tester:
		//m_Player->GetTransform()->SetWorldPosition(m_Player->GetTransform()->GetWorldPosition() + glm::vec3(g_DeltaTime * 1.0f, 0.0f, 0.0f));

		constexpr bool bDrawVelocity = false;
		if (bDrawVelocity)
		{
			real scale = 1.0f;
			btVector3 start = rbPos + btVector3(0.0f, -0.5f, 0.0f);
			btVector3 end = start + rb->getLinearVelocity() * scale;
			debugDrawer->drawLine(start, end, bMaxVel ? btVector3(0.9f, 0.3f, 0.4f) : btVector3(0.1f, 0.85f, 0.98f));
		}

		if (m_Player->m_bPossessed &&
			!bInteractingWithTerminal &&
			!bInteractingWithVehicle &&
			m_Player->m_TrackRidingID == InvalidTrackID)
		{
			real lookH = lookLR;
			// TODO: Remove: will never be hit
			if (m_Player->IsRidingTrack())
			{
				lookH += -g_InputManager->GetActionAxisValue(Action::MOVE_LEFT) + g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);
			}
			real lookV = 0.0f;
			if (m_Mode == Mode::FIRST_PERSON)
			{
				lookV = -g_InputManager->GetActionAxisValue(Action::LOOK_UP) + g_InputManager->GetActionAxisValue(Action::LOOK_DOWN);
			}

			glm::quat rot = transform->GetLocalRotation();
			real angle = lookH * (m_Mode == Mode::FIRST_PERSON ? m_RotateHSpeedFirstPerson : m_RotateHSpeedThirdPerson) * g_DeltaTime;
			rot = glm::rotate(rot, angle, up);
			transform->SetWorldRotation(rot);

			m_Player->AddToPitch(lookV * m_RotateVSpeed * g_DeltaTime);
		}

		m_bAttemptCompleteTrack = false;
		m_bAttemptPlaceItemFromInventory = false;
		m_bAttemptInteract = false;
		m_bAttemptPickup = false;
	}

	void PlayerController::ResetTransformAndVelocities()
	{
		btRigidBody* rb = m_Player->GetRigidBody()->GetRigidBodyInternal();

		rb->clearForces();
		rb->setLinearVelocity(btVector3(0, 0, 0));
		rb->setAngularVelocity(btVector3(0, 0, 0));
		btTransform identity = btTransform::getIdentity();
		identity.setOrigin(btVector3(0, 5, 0));
		rb->setWorldTransform(identity);
	}

	void PlayerController::DrawImGuiObjects()
	{
		const std::string treeName = "Player Controller " + IntToString(m_PlayerIndex);
		if (ImGui::TreeNode(treeName.c_str()))
		{
			ImGui::Text("Seconds attempting to turn: %.5f", m_SecondsAttemptingToTurn);
			ImGui::Text("Turning dir: %s", m_DirTurning == TurningDir::LEFT ? "left" : m_DirTurning == TurningDir::RIGHT ? "right" : "none");

			ImGui::TreePop();
		}
	}

	void PlayerController::SnapPosToTrack(real pDistAlongTrack, bool bReversingDownTrack)
	{
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
		TrackID newTrackID = m_Player->m_TrackRidingID;
		real newDistAlongTrack = m_Player->m_DistAlongTrack;
		LookDirection desiredDir = LookDirection::CENTER;

		const real moveLR = g_InputManager->GetActionAxisValue(Action::MOVE_LEFT) + g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);
		const real lookLR = -g_InputManager->GetActionAxisValue(Action::LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::LOOK_RIGHT);

		if (lookLR > 0.5f)
		{
			desiredDir = LookDirection::RIGHT;
		}
		else if (lookLR < -0.5f)
		{
			desiredDir = LookDirection::LEFT;
		}

		trackManager->UpdatePreview(m_Player->m_TrackRidingID, m_Player->m_DistAlongTrack,
			desiredDir, m_Player->GetTransform()->GetForward(), m_Player->IsFacingDownTrack(),
			bReversingDownTrack);

		i32 newJunctionIndex = -1;
		i32 newCurveIndex = -1;
		TrackState newTrackState = TrackState::_NONE;
		glm::vec3 newPos = trackManager->GetPointOnTrack(m_Player->m_TrackRidingID, m_Player->m_DistAlongTrack,
			pDistAlongTrack, desiredDir, bReversingDownTrack, &newTrackID, &newDistAlongTrack,
			&newJunctionIndex, &newCurveIndex, &newTrackState, false);

		bool bSwitchedTracks = (newTrackID != InvalidTrackID) && (newTrackID != m_Player->m_TrackRidingID);
		if (bSwitchedTracks)
		{
			m_SecondsAttemptingToTurn = 0.0f;
			m_DirTurning = TurningDir::NONE;

			m_Player->m_TrackRidingID = newTrackID;
			m_Player->m_DistAlongTrack = newDistAlongTrack;

			if (newTrackState != TrackState::_NONE)
			{
				m_Player->m_TrackState = newTrackState;
			}
		}

		newPos += glm::vec3(0.0f, m_Player->m_Height / 2.0f, 0.0f);
		m_Player->GetTransform()->SetWorldPosition(newPos);


		bool bTurningRight = moveLR > m_TurnStartStickXThreshold;
		bool bTurningLeft = moveLR < -m_TurnStartStickXThreshold;
		m_DirTurning = bTurningRight ? TurningDir::RIGHT : bTurningLeft ? TurningDir::LEFT : TurningDir::NONE;

		if (m_DirTurning != TurningDir::NONE || m_SecondsAttemptingToTurn < 0.0f)
		{
			m_SecondsAttemptingToTurn += g_DeltaTime;
		}
		else
		{
			if (m_SecondsAttemptingToTurn > 0.0f)
			{
				m_SecondsAttemptingToTurn = 0.0f;
			}
		}

		if (m_SecondsAttemptingToTurn > m_AttemptToTurnTimeThreshold)
		{
			m_SecondsAttemptingToTurn = -m_TurnAroundCooldown;
			m_DirTurning = TurningDir::NONE;
			m_Player->BeginTurnTransition();
		}
	}

	void PlayerController::UpdateMode()
	{
		BaseCamera* cam = g_CameraManager->CurrentCamera();
		m_Mode = cam->bIsFirstPerson ? Mode::FIRST_PERSON : Mode::THIRD_PERSON;
	}

	EventReply PlayerController::OnActionEvent(Action action, ActionEvent actionEvent)
	{
		if (m_Player->m_bPossessed)
		{
			if (action == Action::PLACE_ITEM)
			{
				if (actionEvent == ActionEvent::ACTION_TRIGGER)
				{
					m_bPreviewPlaceItemFromInventory = true;
					return EventReply::CONSUMED;
				}
				else if (actionEvent == ActionEvent::ACTION_RELEASE)
				{
					m_bPreviewPlaceItemFromInventory = false;
					m_bAttemptPlaceItemFromInventory = true;
					return EventReply::CONSUMED;
				}
			}

			if (actionEvent == ActionEvent::ACTION_TRIGGER)
			{
				if (action == Action::SHOW_INVENTORY)
				{
					m_Player->bInventoryShowing = !m_Player->bInventoryShowing;
					g_Window->SetCursorMode(m_Player->bInventoryShowing ? CursorMode::NORMAL : CursorMode::DISABLED);
					return EventReply::CONSUMED;
				}

				if (action == Action::PAUSE)
				{
					g_EngineInstance->SetSimulationPaused(!g_EngineInstance->IsSimulationPaused());
					g_Window->SetCursorMode(CursorMode::NORMAL);
					return EventReply::CONSUMED;
				}

				if (action == Action::ENTER_TRACK_BUILD_MODE)
				{
					if (m_Player->m_TrackRidingID == InvalidTrackID)
					{
						m_Player->m_bPlacingTrack = !m_Player->m_bPlacingTrack;
						m_Player->m_bEditingTrack = false;
						return EventReply::CONSUMED;
					}
				}

				if (action == Action::ENTER_TRACK_EDIT_MODE)
				{
					if (m_Player->m_TrackRidingID == InvalidTrackID)
					{
						m_Player->m_bEditingTrack = !m_Player->m_bEditingTrack;
						m_Player->m_bPlacingTrack = false;
						return EventReply::CONSUMED;
					}
				}

				if (action == Action::COMPLETE_TRACK)
				{
					if (m_Player->m_TrackRidingID == InvalidTrackID)
					{
						m_bAttemptCompleteTrack = true;
						return EventReply::CONSUMED;
					}
				}
				if (action == Action::PICKUP_ITEM)
				{
					if (!m_Player->bInventoryShowing && m_Player->m_bPossessed && m_Player->m_TrackRidingID == InvalidTrackID)
					{
						m_bAttemptPickup = true;
						return EventReply::CONSUMED;
					}
				}

				if (action == Action::TOGGLE_ITEM_HOLDING)
				{
					//if (m_Player->m_HeldItem == nullptr)
					//{
					//	if (!m_Player->m_Inventory.empty())
					//	{
					//		m_Player->m_HeldItem = m_Player->m_Inventory[0];
					//	}
					//}
					//else
					//{
					//	m_Player->m_HeldItem = nullptr;
					//}
					return EventReply::CONSUMED;
				}

				if (action == Action::TOGGLE_TABLET)
				{
					m_Player->m_bTabletUp = !m_Player->m_bTabletUp;
					return EventReply::CONSUMED;
				}

				if (action == Action::INTERACT)
				{
					m_bAttemptInteract = true;
					// TODO: Determine if we can interact with anything here to allow
					// others to consume this event in the case we don't want it
					return EventReply::CONSUMED;
				}
			}
			else if (actionEvent == ActionEvent::ACTION_RELEASE)
			{
				if (action == Action::PICKUP_ITEM)
				{
					if (m_ItemPickingTimer != -1.0f)
					{
						m_ItemPickingTimer = -1.0f;
						return EventReply::CONSUMED;
					}
				}
			}
		}

		return EventReply::UNCONSUMED;
	}

	EventReply PlayerController::OnMouseMovedEvent(const glm::vec2& dMousePos)
	{
		if (m_Player != nullptr && !m_Player->bInventoryShowing && g_Window->HasFocus())
		{
			bool bInteractingWithTerminal = false;
			bool bInteractingWithVehicle = false;
			{
				GameObject* objInteractingWith = m_Player->GetObjectInteractingWith();
				bInteractingWithTerminal = (objInteractingWith != nullptr) && (objInteractingWith->GetTypeID() == SID("terminal"));
				bInteractingWithVehicle = (objInteractingWith != nullptr) && (objInteractingWith->GetTypeID() == SID("vehicle"));
			}

			if (m_Player->m_bPossessed &&
				!bInteractingWithTerminal &&
				!bInteractingWithVehicle &&
				m_Player->m_TrackRidingID == InvalidTrackID)
			{
				real lookH = dMousePos.x;
				real lookV = dMousePos.y;

				Transform* transform = m_Player->GetTransform();
				glm::vec3 up = transform->GetUp();
				glm::quat rot = transform->GetLocalRotation();
				real angle = lookH * m_MouseRotateHSpeed * g_DeltaTime;
				rot = glm::rotate(rot, angle, up);
				transform->SetWorldRotation(rot);

				m_Player->AddToPitch(lookV * m_MouseRotateVSpeed * g_DeltaTime * (m_bInvertMouseV ? -1.0f : 1.0f));

				return EventReply::CONSUMED;
			}
		}

		return EventReply::UNCONSUMED;
	}

} // namespace flex
