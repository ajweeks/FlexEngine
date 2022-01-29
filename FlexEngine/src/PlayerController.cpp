#include "stdafx.hpp"

#include "PlayerController.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
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
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/TrackManager.hpp"
#include "UIMesh.hpp"
#include "Window/Window.hpp"

namespace flex
{
	PlayerController::PlayerController() :
		m_ActionCallback(this, &PlayerController::OnActionEvent),
		m_MouseMovedCallback(this, &PlayerController::OnMouseMovedEvent),
		m_ConfigFile("Player config", PLAYER_CONFIG_LOCATION, CURRENT_CONFIG_FILE_VERSION)
	{
		m_ConfigFile.RegisterProperty("max move speed", &m_MaxMoveSpeed);
		m_ConfigFile.RegisterProperty("rotate h speed first person", &m_RotateHSpeedFirstPerson);
		m_ConfigFile.RegisterProperty("rotate h speed third person", &m_RotateHSpeedThirdPerson);
		m_ConfigFile.RegisterProperty("rotate v speed", &m_RotateVSpeed);
		m_ConfigFile.RegisterProperty("mouse rotate h speed", &m_MouseRotateHSpeed)
			.Range(0.01f, 10.0f);
		m_ConfigFile.RegisterProperty("mouse rotate v speed", &m_MouseRotateVSpeed)
			.Range(0.01f, 10.0f);
		m_ConfigFile.RegisterProperty("invert move v", &m_bInvertMouseV);
	}

	PlayerController::~PlayerController()
	{
	}

	void PlayerController::Initialize(Player* player)
	{
		m_Player = player;
		m_PlayerIndex = m_Player->GetIndex();

		CHECK(m_PlayerIndex == 0 || m_PlayerIndex == 1);

		m_Player->UpdateIsPossessed();

		g_InputManager->BindActionCallback(&m_ActionCallback, 14);
		g_InputManager->BindMouseMovedCallback(&m_MouseMovedCallback, 14);

		m_PlaceItemAudioID = g_ResourceManager->GetOrLoadAudioSourceID(SID("drip-01.wav"), true);
		m_PlaceItemFailureAudioID = g_ResourceManager->GetOrLoadAudioSourceID(SID("spook-01.wav"), true);

		LoadConfigFile();
	}

	void PlayerController::Destroy()
	{
		g_InputManager->UnbindActionCallback(&m_ActionCallback);
		g_InputManager->UnbindMouseMovedCallback(&m_MouseMovedCallback);

		delete m_ItemPlacementBoundingBoxShape;
		m_ItemPlacementBoundingBoxShape = nullptr;
	}

	void PlayerController::Update()
	{
		if (m_Player->m_bPossessed &&
			g_Window->HasFocus() &&
			!g_EngineInstance->IsSimulationPaused())
		{
			g_Window->SetCursorMode(m_Player->IsInventoryShowing() ? CursorMode::NORMAL : CursorMode::DISABLED);
		}

		// TODO: Make frame-rate-independent!

		btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();

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
			real cycleItemAxis = g_InputManager->GetActionAxisValue(Action::CYCLE_SELECTED_ITEM_FORWARD);
			if (!ImGui::GetIO().WantCaptureMouse)
			{
				if (cycleItemAxis > 0.0f)
				{
					m_Player->selectedItemSlot = (m_Player->selectedItemSlot + 1) % Player::QUICK_ACCESS_ITEM_COUNT;
				}
				else if (cycleItemAxis < 0.0f)
				{
					m_Player->selectedItemSlot = (m_Player->selectedItemSlot - 1 + Player::QUICK_ACCESS_ITEM_COUNT) % Player::QUICK_ACCESS_ITEM_COUNT;
				}
			}

			if (m_Player->m_TrackRidingID == InvalidTrackID)
			{
				TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

				if (m_Player->m_bPlacingTrack)
				{
					glm::vec3 reticlePos = m_Player->GetTrackPlacementReticlePosWS(1.0f);

					if (m_bAttemptInteractLeftHand || m_bAttemptInteractRightHand)
					{
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

						m_bAttemptInteractLeftHand = false;
						m_bAttemptInteractRightHand = false;
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
					if (m_bAttemptInteractLeftHand || m_bAttemptInteractRightHand)
					{
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

						m_bAttemptInteractLeftHand = false;
						m_bAttemptInteractRightHand = false;
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

					GameObjectStack::UserData itemUserData = {};
					PrefabID itemID = m_ItemPickingUp->Itemize(itemUserData);
					m_Player->AddToInventory(itemID, 1, itemUserData);
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

			std::list<Pair<GameObject*, real>> nearbyInteractables;

			GameObject* nearestInteractable = nullptr;
			if (!m_Player->ridingVehicleID.IsValid() &&
				!m_Player->terminalInteractingWithID.IsValid())
			{
				std::vector<GameObject*> allInteractableObjects;
				g_SceneManager->CurrentScene()->GetInteractableObjects(allInteractableObjects);

				if (!allInteractableObjects.empty())
				{
					// List of objects sorted by sqr dist (closest to farthest)

					// TODO: Move to All.variables
					real maxDistSqr = 7.0f * 7.0f;
					const glm::vec3 pos = m_Player->m_Transform.GetWorldPosition();
					for (GameObject* obj : allInteractableObjects)
					{
						if (obj != m_Player)
						{
							// TODO: Also check player is facing towards this object
							real dist2 = glm::distance2(obj->GetTransform()->GetWorldPosition(), pos);
							if (dist2 <= maxDistSqr)
							{
								nearbyInteractables.emplace_back(obj, dist2);
							}
						}
					}

					if (!nearbyInteractables.empty())
					{
						nearbyInteractables.sort([](const Pair<GameObject*, real>& a, const Pair<GameObject*, real>& b)
						{
							return a.second < b.second;
						});

						nearestInteractable = nearbyInteractables.front().first;
						nearestInteractable->SetNearbyInteractable(m_Player);
						m_Player->SetNearbyInteractable(nearestInteractable);
					}
				}
			}

			if (m_bAttemptInteractLeftHand || m_bAttemptInteractRightHand)
			{
				bool bPlaceItemLeft = m_bAttemptInteractLeftHand && m_Player->heldItemLeftHand.IsValid();
				bool bPlaceItemRight = m_bAttemptInteractRightHand && m_Player->heldItemRightHand.IsValid();

				bool bPickupItemLeft = m_bAttemptInteractLeftHand && !m_Player->heldItemLeftHand.IsValid();
				bool bPickupItemRight = m_bAttemptInteractRightHand && !m_Player->heldItemRightHand.IsValid();

				if (bPlaceItemLeft || bPlaceItemRight)
				{
					// Toggle interaction when already interacting
					GameObject* heldItemLeftHand = m_Player->heldItemLeftHand.Get();
					GameObject* heldItemRightHand = m_Player->heldItemRightHand.Get();
					bool bPlaceWireLeft = bPlaceItemLeft && heldItemLeftHand->GetTypeID() == WirePlugSID;
					bool bPlaceWireRight = bPlaceItemRight && heldItemRightHand->GetTypeID() == WirePlugSID;
					if (bPlaceWireLeft || bPlaceWireRight)
					{
						WirePlug* wirePlug = (WirePlug*)(bPlaceWireLeft ? heldItemLeftHand : heldItemRightHand);
						// Plug in wire!
						PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
						if (pluggablesSystem->PlugInToNearbySocket(wirePlug, WirePlug::nearbyThreshold))
						{
							if (bPlaceWireLeft)
							{
								m_Player->heldItemLeftHand = InvalidGameObjectID;
							}
							else
							{
								m_Player->heldItemRightHand = InvalidGameObjectID;
							}

							heldItemLeftHand = m_Player->heldItemLeftHand.Get();
							heldItemRightHand = m_Player->heldItemRightHand.Get();
							if ((heldItemLeftHand == nullptr || heldItemLeftHand->GetTypeID() != WirePlugSID) &&
								(heldItemRightHand == nullptr || heldItemRightHand->GetTypeID() != WirePlugSID))
							{
								m_PlacingWire = nullptr;
							}
						}
					}
				}
				else if (bPickupItemLeft || bPickupItemRight)
				{
					auto nearbyInteractableIter = nearbyInteractables.begin();
					bool bInteracted = false;
					// TODO: Run this loop every frame to indicate to player what each hand will interact with
					while (!bInteracted && nearbyInteractableIter != nearbyInteractables.end())
					{
						GameObject* nearbyInteractable = nearbyInteractableIter->first;

						switch (nearbyInteractable->GetTypeID())
						{
						case SocketSID:
						case WirePlugSID:
						{
							WirePlug* wirePlugInteractingWith = nullptr;

							if (nearbyInteractable->GetTypeID() == SocketSID)
							{
								// Interacting with full socket, unplug wire
								Socket* socket = (Socket*)nearbyInteractable;
								wirePlugInteractingWith = (WirePlug*)socket->connectedPlugID.Get();

								if (wirePlugInteractingWith != nullptr && m_Player->IsHolding(wirePlugInteractingWith))
								{
									// Can't interact with plug player is already holding
									// TODO: Call generic `IsInteractable` on plug
									break;
								}
							}
							else
							{
								wirePlugInteractingWith = (WirePlug*)nearbyInteractable;
								if (wirePlugInteractingWith != nullptr && m_Player->IsHolding(wirePlugInteractingWith))
								{

									// Can't interact with plug player is already holding
									break;
								}
							}

							if (wirePlugInteractingWith != nullptr)
							{
								PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);

								if (wirePlugInteractingWith->socketID.IsValid())
								{
									// Plug is plugged in, try unplugging
									pluggablesSystem->UnplugFromSocket(wirePlugInteractingWith);
									bInteracted = true;
									if (bPickupItemLeft)
									{
										m_Player->heldItemLeftHand = wirePlugInteractingWith->ID;
									}
									else if (bPickupItemRight)
									{
										m_Player->heldItemRightHand = wirePlugInteractingWith->ID;
									}
								}
								else
								{
									// Plug is not plugged in, grab plug
									bInteracted = true;
									if (bPickupItemLeft)
									{
										m_Player->heldItemLeftHand = wirePlugInteractingWith->ID;
									}
									else if (bPickupItemRight)
									{
										m_Player->heldItemRightHand = wirePlugInteractingWith->ID;
									}
								}
							}
						} break;
						case TerminalSID:
						{
							Terminal* terminal = (Terminal*)nearbyInteractable;
							m_Player->SetInteractingWithTerminal(terminal);
						} break;
						case SpeakerSID:
						{
							Speaker* speaker = (Speaker*)nearbyInteractable;
							speaker->TogglePlaying();
						} break;
						case MinerSID:
						{
							if (!m_Player->bInventoryShowing)
							{
								m_Player->bMinerInventoryShowing = !m_Player->bMinerInventoryShowing;
							}
						} break;
						}

						++nearbyInteractableIter;
					}
				}

				m_bAttemptInteractLeftHand = false;
				m_bAttemptInteractRightHand = false;
			}
		}

		if (m_bCancelPlaceItemFromInventory)
		{
			m_bPreviewPlaceItemFromInventory = false;
		}

		if (m_bPreviewPlaceItemFromInventory)
		{
			if (m_Player->selectedItemSlot == -1)
			{
				m_Player->selectedItemSlot = 0;
			}

			GameObjectStack& gameObjectStack = m_Player->m_QuickAccessInventory[m_Player->selectedItemSlot];

			if (gameObjectStack.count >= 1)
			{
				if (gameObjectStack.prefabID.IsValid())
				{
					// TODO: Interpolate towards target point
					Transform* playerTransform = m_Player->GetTransform();
					m_TargetItemPlacementPos = playerTransform->GetWorldPosition() +
						playerTransform->GetForward() * 4.0f +
						playerTransform->GetUp() * 1.0f;
					m_TargetItemPlacementRot = playerTransform->GetWorldRotation();

					GameObject* templateObject = g_ResourceManager->GetPrefabTemplate(gameObjectStack.prefabID);
					Mesh* mesh = templateObject->GetMesh();

					if (mesh != nullptr)
					{
						static const glm::vec4 validColour(2.0f, 4.0f, 2.5f, 0.4f);
						static const glm::vec4 invalidColour(4.0f, 2.0f, 2.0f, 0.4f);
						g_Renderer->QueueHologramMesh(gameObjectStack.prefabID,
							m_TargetItemPlacementPos,
							m_TargetItemPlacementRot,
							VEC3_ONE,
							m_bItemPlacementValid ? validColour : invalidColour);
					}
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

			if (m_Player->selectedItemSlot == -1)
			{
				m_Player->selectedItemSlot = 0;
			}

			if (m_bItemPlacementValid)
			{
				GameObjectStack& gameObjectStack = m_Player->m_QuickAccessInventory[m_Player->selectedItemSlot];

				if (gameObjectStack.count >= 1)
				{
					if (gameObjectStack.prefabID.IsValid())
					{
						GameObject* gameObject = GameObject::Deitemize(gameObjectStack.prefabID, m_TargetItemPlacementPos, m_TargetItemPlacementRot, gameObjectStack.userData);
						if (gameObject != nullptr)
						{
							// Add non-immediate
							g_SceneManager->CurrentScene()->AddRootObject(gameObject);
							gameObjectStack.count--;

							if (gameObjectStack.count == 0)
							{
								gameObjectStack.Clear();
							}

							AudioManager::PlaySource(m_PlaceItemAudioID);
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
			else
			{
				AudioManager::PlaySource(m_PlaceItemFailureAudioID);
			}
		}

		if (m_bCancelPlaceWire)
		{
			m_bCancelPlaceWire = false;

			if (m_PlacingWire != nullptr)
			{
				g_SceneManager->CurrentScene()->RemoveObject(m_PlacingWire, true);
				m_PlacingWire = nullptr;
			}
		}

		if (m_bSpawnWire)
		{
			m_bSpawnWire = false;

			if (m_PlacingWire == nullptr)
			{
				BaseScene* currentScene = g_SceneManager->CurrentScene();
				m_PlacingWire = (Wire*)GameObject::CreateObjectOfType(WireSID, currentScene->GetUniqueObjectName("wire_", 3));

				Transform* playerTransform = m_Player->GetTransform();
				Transform* wireTransform = m_PlacingWire->GetTransform();

				glm::vec3 targetPos = playerTransform->GetWorldPosition() + playerTransform->GetForward() * 2.0f;
				glm::quat targetRot = playerTransform->GetWorldRotation();

				wireTransform->SetWorldPosition(targetPos, false);
				wireTransform->SetWorldRotation(targetRot, true);

				currentScene->AddRootObject(m_PlacingWire);

				CHECK(!m_Player->heldItemLeftHand.IsValid());
				CHECK(!m_Player->heldItemRightHand.IsValid());

				m_Player->heldItemLeftHand = m_PlacingWire->plug0ID;
				m_Player->heldItemRightHand = m_PlacingWire->plug1ID;
			}
		}

		if (m_bDropItem)
		{
			m_bDropItem = false;

			m_Player->DropSelectedItem();
		}

		if (m_Player->objectInteractingWithID.IsValid())
		{
			GameObject* objectInteractingWith = m_Player->objectInteractingWithID.Get();
			if (objectInteractingWith->GetTypeID() == ValveSID)
			{
				Valve* valve = (Valve*)objectInteractingWith;

				const GamepadState& gamepadState = g_InputManager->GetGamepadState(m_PlayerIndex);
				valve->rotationSpeed = (-gamepadState.averageRotationSpeeds.currentAverage) * valve->rotationSpeedScale;
			}
		}

		btRigidBody* rb = m_Player->GetRigidBody()->GetRigidBodyInternal();
		btVector3 rbPos = rb->getInterpolationWorldTransform().getOrigin();

		Transform* playerTransform = m_Player->GetTransform();
		glm::vec3 up = playerTransform->GetUp();
		glm::vec3 right = playerTransform->GetRight();
		glm::vec3 forward = playerTransform->GetForward();
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

		if (m_Player->m_bPossessed)
		{
			if (g_InputManager->IsGamepadButtonDown(m_PlayerIndex, GamepadButton::BACK))
			{
				ResetTransformAndVelocities();
				return;
			}

			if (m_bAttemptInteractLeftHand)
			{
				if (m_Player->m_TrackRidingID == InvalidTrackID)
				{
					real distAlongTrack = -1.0f;
					TrackID trackInRangeIndex = trackManager->GetTrackInRangeID(playerTransform->GetWorldPosition(), m_Player->m_TrackAttachMinDist, &distAlongTrack);
					if (trackInRangeIndex != InvalidTrackID)
					{
						m_bAttemptInteractLeftHand = false;

						m_Player->AttachToTrack(trackInRangeIndex, distAlongTrack);

						SnapPosToTrack(m_Player->m_DistAlongTrack, false);
					}
				}
				else
				{
					m_bAttemptInteractLeftHand = false;
					m_Player->DetachFromTrack();
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

		m_bAttemptCompleteTrack = false;
		m_bAttemptPlaceItemFromInventory = false;
		m_bAttemptInteractLeftHand = false;
		m_bAttemptPickup = false;
	}

	void PlayerController::FixedUpdate()
	{
		btVector3 force(0.0f, 0.0f, 0.0f);

		Transform* playerTransform = m_Player->GetTransform();
		glm::vec3 playerUp = playerTransform->GetUp();
		glm::vec3 playerRight = playerTransform->GetRight();
		glm::vec3 playerForward = playerTransform->GetForward();
		btRigidBody* rb = m_Player->GetRigidBody()->GetRigidBodyInternal();

		const real moveLR = -g_InputManager->GetActionAxisValue(Action::MOVE_LEFT) + g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);
		const real moveFB = -g_InputManager->GetActionAxisValue(Action::MOVE_BACKWARD) + g_InputManager->GetActionAxisValue(Action::MOVE_FORWARD);
		const real lookLR = -g_InputManager->GetActionAxisValue(Action::LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::LOOK_RIGHT);

		TrackID pTrackRidingID = m_Player->m_TrackRidingID;
		bool bWasFacingDownTrack = m_Player->IsFacingDownTrack();

		const btVector3& vel = rb->getLinearVelocity();
		btVector3 xzVel(vel.getX(), 0, vel.getZ());
		real xzVelMagnitude = xzVel.length();
		if (xzVelMagnitude > m_MaxMoveSpeed)
		{
			// Limit maximum velocity
			btVector3 xzVelNorm = xzVel.normalized();
			btVector3 newVel(xzVelNorm.getX() * m_MaxMoveSpeed, vel.getY(), xzVelNorm.getZ() * m_MaxMoveSpeed);
			rb->setLinearVelocity(newVel);
		}

		m_Player->UpdateIsGrounded();

		if (m_bPreviewPlaceItemFromInventory)
		{
			if (m_Player->selectedItemSlot == -1)
			{
				m_Player->selectedItemSlot = 0;
			}

			GameObjectStack& gameObjectStack = m_Player->m_QuickAccessInventory[m_Player->selectedItemSlot];

			if (gameObjectStack.count >= 1)
			{
				if (gameObjectStack.prefabID.IsValid())
				{
					// TODO: Interpolate towards target point
					m_TargetItemPlacementPos = playerTransform->GetWorldPosition() +
						playerForward * 4.0f +
						playerUp * 1.0f;
					m_TargetItemPlacementRot = playerTransform->GetWorldRotation();

					GameObject* templateObject = g_ResourceManager->GetPrefabTemplate(gameObjectStack.prefabID);
					Mesh* mesh = templateObject->GetMesh();

					if (mesh != nullptr)
					{
						btVector3 boxHalfExtents = ToBtVec3((mesh->m_MaxPoint - mesh->m_MinPoint) * 0.5f);
						btTransform bbTransform(ToBtQuaternion(m_TargetItemPlacementRot), ToBtVec3(m_TargetItemPlacementPos));
						PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();

						if (m_ItemPlacementBoundingBoxShape == nullptr)
						{
							m_ItemPlacementBoundingBoxShape = new btBoxShape(boxHalfExtents);
						}

						btPairCachingGhostObject pairCache;

						pairCache.setCollisionShape(m_ItemPlacementBoundingBoxShape);
						pairCache.setWorldTransform(bbTransform);

						i32 mask = (u32)CollisionType::NOTHING;
						pairCache.setCollisionFlags(mask);

						CustomContactResultCallback resultCallback;
						resultCallback.m_collisionFilterGroup = mask;
						resultCallback.m_collisionFilterMask = mask;
						physicsWorld->GetWorld()->contactTest(&pairCache, resultCallback);

						m_bItemPlacementValid = !resultCallback.bHit;
					}
				}
			}
		}

		if (m_Player->m_bPossessed &&
			!m_Player->terminalInteractingWithID.IsValid() &&
			!m_Player->ridingVehicleID.IsValid() &&
			m_Player->m_TrackRidingID == InvalidTrackID)
		{
			real lookH = lookLR;
			real lookV = 0.0f;
			if (m_Mode == Mode::FIRST_PERSON)
			{
				lookV = -g_InputManager->GetActionAxisValue(Action::LOOK_UP) + g_InputManager->GetActionAxisValue(Action::LOOK_DOWN);
			}

			glm::quat rot = playerTransform->GetLocalRotation();
			real angle = lookH * (m_Mode == Mode::FIRST_PERSON ? m_RotateHSpeedFirstPerson : m_RotateHSpeedThirdPerson) * g_FixedDeltaTime;
			rot = glm::rotate(rot, angle, playerUp);
			playerTransform->SetWorldRotation(rot);

			m_Player->AddToPitch(lookV * m_RotateVSpeed * g_FixedDeltaTime);
		}

		if (m_Player->m_bPossessed)
		{
			if (m_Player->m_TrackRidingID != InvalidTrackID)
			{
				TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

				glm::vec3 newCurveDir = trackManager->GetTrack(m_Player->m_TrackRidingID)->GetCurveDirectionAt(m_Player->m_DistAlongTrack);
				static glm::vec3 pCurveDir = newCurveDir;

				real move = moveFB;

				const bool bReversing = (move < 0.0f);

				if (!m_Player->IsFacingDownTrack())
				{
					move = -move;
				}

				real targetDDist = move * m_Player->m_TrackMoveSpeed * g_FixedDeltaTime;
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
				if (!m_Player->terminalInteractingWithID.IsValid() &&
					!m_Player->ridingVehicleID.IsValid() &&
					!m_Player->IsInventoryShowing())
				{
					real moveAcceleration = TWEAKABLE(80000.0f);

					force += ToBtVec3(playerRight) * moveAcceleration * moveLR * g_FixedDeltaTime;
					force += ToBtVec3(playerForward) * moveAcceleration * moveFB * g_FixedDeltaTime;
				}
			}
		}

		rb->applyCentralForce(force);
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

			ConfigFile::Request request = m_ConfigFile.DrawImGuiObjects();
			switch (request)
			{
			case ConfigFile::Request::SERIALIZE:
				SerializeConfigFile();
				break;
			case ConfigFile::Request::RELOAD:
				LoadConfigFile();
				break;
			}

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

	void PlayerController::LoadConfigFile()
	{
		m_ConfigFile.Deserialize();
	}

	void PlayerController::SerializeConfigFile()
	{
		m_ConfigFile.Serialize();
	}

	void PlayerController::UpdateMode()
	{
		BaseCamera* cam = g_CameraManager->CurrentCamera();
		m_Mode = cam->bIsFirstPerson ? Mode::FIRST_PERSON : Mode::THIRD_PERSON;
	}

	EventReply PlayerController::OnActionEvent(Action action, ActionEvent actionEvent)
	{
		if (action == Action::PAUSE && actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			g_EngineInstance->SetSimulationPaused(!g_EngineInstance->IsSimulationPaused());
			g_Window->SetCursorMode(CursorMode::NORMAL);
			return EventReply::CONSUMED;
		}

		if (g_EngineInstance->IsSimulationPaused() || !m_Player->m_bPossessed)
		{
			return EventReply::UNCONSUMED;
		}

		if (action == Action::PAUSE)
		{
			if (m_bPreviewPlaceItemFromInventory)
			{
				m_bCancelPlaceItemFromInventory = true;
				return EventReply::CONSUMED;
			}
			if (m_PlacingWire != nullptr)
			{
				m_bCancelPlaceWire = true;
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::SHOW_INVENTORY &&
			!m_Player->bMinerInventoryShowing &&
			actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			m_Player->bInventoryShowing = !m_Player->bInventoryShowing;
			g_Window->SetCursorMode(m_Player->bInventoryShowing ? CursorMode::NORMAL : CursorMode::DISABLED);
			return EventReply::CONSUMED;
		}

		if (action == Action::INTERACT_LEFT_HAND && actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			m_bAttemptInteractLeftHand = true;
			// TODO: Determine if we can interact with anything here to allow
			// others to consume this event in the case we don't want it
			return EventReply::CONSUMED;
		}
		if (action == Action::INTERACT_RIGHT_HAND && actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			m_bAttemptInteractRightHand = true;
			// TODO: Determine if we can interact with anything here to allow
			// others to consume this event in the case we don't want it
			return EventReply::CONSUMED;
		}

		if (m_Player->IsInventoryShowing())
		{
			return EventReply::UNCONSUMED;
		}

		if (action == Action::PLACE_ITEM && !m_bSpawnWire)
		{
			if (actionEvent == ActionEvent::ACTION_TRIGGER)
			{
				m_bItemPlacementValid = false;
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

		if (action == Action::PLACE_WIRE && !m_bPreviewPlaceItemFromInventory
			&& !m_Player->heldItemLeftHand.IsValid() && !m_Player->heldItemRightHand.IsValid())
		{
			if (actionEvent == ActionEvent::ACTION_TRIGGER)
			{
				m_bSpawnWire = true;
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::DROP_ITEM)
		{
			if (m_Player->HasFullSelectedInventorySlot() && actionEvent == ActionEvent::ACTION_TRIGGER)
			{
				m_bDropItem = true;
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::ENTER_TRACK_BUILD_MODE && actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			if (m_Player->m_TrackRidingID == InvalidTrackID)
			{
				m_Player->m_bPlacingTrack = !m_Player->m_bPlacingTrack;
				m_Player->m_bEditingTrack = false;
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::ENTER_TRACK_EDIT_MODE && actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			if (m_Player->m_TrackRidingID == InvalidTrackID)
			{
				m_Player->m_bEditingTrack = !m_Player->m_bEditingTrack;
				m_Player->m_bPlacingTrack = false;
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::COMPLETE_TRACK && actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			if (m_Player->m_TrackRidingID == InvalidTrackID)
			{
				m_bAttemptCompleteTrack = true;
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::PICKUP_ITEM)
		{
			if (actionEvent == ActionEvent::ACTION_TRIGGER)
			{
				if (m_Player->m_bPossessed && m_Player->m_TrackRidingID == InvalidTrackID)
				{
					m_bAttemptPickup = true;
					return EventReply::CONSUMED;
				}
			}
			else if (actionEvent == ActionEvent::ACTION_RELEASE)
			{
				if (m_ItemPickingTimer != -1.0f)
				{
					m_ItemPickingTimer = -1.0f;
					return EventReply::CONSUMED;
				}
			}
		}

		if (action == Action::TOGGLE_ITEM_HOLDING && actionEvent == ActionEvent::ACTION_TRIGGER)
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

		if (action == Action::TOGGLE_TABLET && actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			m_Player->m_bTabletUp = !m_Player->m_bTabletUp;
			return EventReply::CONSUMED;
		}

		return EventReply::UNCONSUMED;
	}

	EventReply PlayerController::OnMouseMovedEvent(const glm::vec2& dMousePos)
	{
		if (m_Player != nullptr && !m_Player->IsInventoryShowing() &&
			g_Window->HasFocus() && !g_EngineInstance->IsSimulationPaused())
		{
			if (m_Player->m_bPossessed &&
				!m_Player->terminalInteractingWithID.IsValid() &&
				!m_Player->ridingVehicleID.IsValid() &&
				m_Player->m_TrackRidingID == InvalidTrackID)
			{
				real lookH = dMousePos.x;
				real lookV = dMousePos.y;

				Transform* transform = m_Player->GetTransform();
				glm::vec3 up = transform->GetUp();
				glm::quat rot = transform->GetLocalRotation();
				real angle = lookH * m_MouseRotateHSpeed * 0.001f;
				rot = glm::rotate(rot, angle, up);
				transform->SetWorldRotation(rot);

				m_Player->AddToPitch(lookV * m_MouseRotateVSpeed * 0.001f * (m_bInvertMouseV ? -1.0f : 1.0f));

				return EventReply::CONSUMED;
			}
		}

		return EventReply::UNCONSUMED;
	}
} // namespace flex
