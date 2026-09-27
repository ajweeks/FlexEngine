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
IGNORE_WARNINGS_POP

#include <list>

#include "Audio/AudioManager.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/FirstPersonCamera.hpp"
#include "Cameras/OverheadCamera.hpp"
#include "FlexEngine.hpp"
#include "Graphics/DebugRenderer.hpp"
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
		m_TargetItemPlacementPos(FLT_MAX),
		m_TargetItemPlacementPosSmoothed(FLT_MAX),
		m_TargetItemPlacementRot(glm::vec3(FLT_MAX)),
		m_TargetItemPlacementRotSmoothed(glm::vec3(FLT_MAX)),
		m_ItemPlacementGroundedPos(FLT_MAX),
		m_ConfigFile("Player config", PLAYER_CONFIG_LOCATION, CURRENT_CONFIG_FILE_VERSION)
	{
		m_ConfigFile.RegisterProperty("max move speed", &m_MaxMoveSpeed)
			.SetRange(0.001f, 100.0f);
		m_ConfigFile.RegisterProperty("move acceleration", &m_MoveAcceleration)
			.SetRange(0.1f, 500.0f);
		m_ConfigFile.RegisterProperty("move deceleration", &m_MoveDeceleration)
			.SetRange(0.1f, 500.0f);
		m_ConfigFile.RegisterProperty("camera elevation sensitivity", &m_CameraElevationSensitivity)
			.SetRange(0.01f, 10.0f);
		m_ConfigFile.RegisterProperty("rotate h speed first person", &m_RotateHSpeedFirstPerson);
		m_ConfigFile.RegisterProperty("rotate h speed third person", &m_RotateHSpeedThirdPerson);
		m_ConfigFile.RegisterProperty("rotate v speed", &m_RotateVSpeed);
		m_ConfigFile.RegisterProperty("mouse rotate h speed", &m_MouseRotateHSpeed)
			.SetRange(0.01f, 10.0f);
		m_ConfigFile.RegisterProperty("mouse rotate v speed", &m_MouseRotateVSpeed)
			.SetRange(0.01f, 10.0f);
		m_ConfigFile.RegisterProperty("invert move v", &m_bInvertMouseV);
		m_ConfigFile.RegisterProperty("look smoothing time", &m_LookSmoothingTime)
			.SetRange(0.0f, 0.2f);
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
			g_Window->SetCursorMode(m_Player->IsAnyInventoryShowing() ? CursorMode::NORMAL : CursorMode::DISABLED);
		}

		UpdateLook();

		// TODO: Make frame-rate-independent!

		DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();

		if (m_Player->m_bPossessed)
		{
			real cycleItemAxis = g_InputManager->GetActionAxisValue(Action::CYCLE_SELECTED_ITEM_FORWARD);
			if (!ImGui::GetIO().WantCaptureMouse)
			{
				if (cycleItemAxis > 0.0f)
				{
					m_Player->SetSelectedQuickAccessItemSlot((m_Player->m_SelectedQuickAccessItemSlot + 1) % Player::QUICK_ACCESS_ITEM_COUNT);
				}
				else if (cycleItemAxis < 0.0f)
				{
					m_Player->SetSelectedQuickAccessItemSlot((m_Player->m_SelectedQuickAccessItemSlot - 1 + Player::QUICK_ACCESS_ITEM_COUNT) % Player::QUICK_ACCESS_ITEM_COUNT);
				}
				CHECK(m_Player->m_SelectedQuickAccessItemSlot >= 0 && m_Player->m_SelectedQuickAccessItemSlot < (i32)m_Player->m_QuickAccessInventory.size());
			}

			if (m_Player->m_TrackRidingID == InvalidTrackID)
			{
				if (m_Player->IsPlacingTrack() || m_Player->IsEditingTrack())
				{
					// Update the reticle before acting on it
					m_Player->UpdateTrackBuilding();

					// Only LMB & right hand interact (E) act as the primary track building action, other interactions are ignored
					if (m_bAttemptTrackBuildingPrimary || m_bAttemptInteractRightHand)
					{
						m_Player->OnTrackBuildingPrimaryAction();
					}
					m_bAttemptTrackBuildingPrimary = false;
					m_bAttemptInteractLeftHand = false;
					m_bAttemptInteractRightHand = false;
					m_bAttemptPickup = false;

					if (m_bAttemptUndoTrackNode)
					{
						m_Player->OnTrackBuildingUndoAction();
						m_bAttemptUndoTrackNode = false;
					}

					if (m_bAttemptDeleteTrack)
					{
						m_Player->OnTrackBuildingDeleteTrackAction();
						m_bAttemptDeleteTrack = false;
					}

					if (m_bAttemptCompleteTrack)
					{
						if (m_Player->IsPlacingTrack())
						{
							m_Player->AttemptCompleteTrack();
						}
						m_bAttemptCompleteTrack = false;
					}
				}

				if (m_bAttemptPickup)
				{
					m_bAttemptPickup = false;

					GameObject* pickedItem = m_Player->GetObjectPointedAt();
					if (pickedItem != nullptr)
					{
						m_Player->SetItemPickingUp(pickedItem);
					}
				}
			}

			// List of objects sorted by square dist (closest to farthest)
			std::list<Pair<GameObject*, real>> nearbyInteractables;

			GameObject* nearestInteractable = nullptr;
			if (m_Player->AbleToInteract())
			{
				// TODO: Move to All.variables
				const real maxDistSqr = 9.0f * 9.0f;
				const glm::vec3 posWS = m_Player->m_Transform.GetWorldPosition();
				GameObjectID excludeGameObjectID = m_Player->ID;
				g_SceneManager->CurrentScene()->GetNearbyInteractableObjects(nearbyInteractables,
					posWS, maxDistSqr, excludeGameObjectID);

				if (!nearbyInteractables.empty())
				{
					nearestInteractable = nearbyInteractables.front().first;
					nearestInteractable->SetNearbyInteractable(m_Player->ID);
					m_Player->SetNearbyInteractable(nearestInteractable->ID);
				}
			}

			if (m_bAttemptInteractLeftHand || m_bAttemptInteractRightHand)
			{
				const bool bLeftHandFilled = m_Player->GetHeldItem(Hand::LEFT).IsValid();
				const bool bRightHandFilled = m_Player->GetHeldItem(Hand::RIGHT).IsValid();

				const bool bPlaceItemLeft = m_bAttemptInteractLeftHand && bLeftHandFilled;
				const bool bPlaceItemRight = m_bAttemptInteractRightHand && bRightHandFilled;

				const bool bPickupItemLeft = m_bAttemptInteractLeftHand && !bLeftHandFilled;
				const bool bPickupItemRight = m_bAttemptInteractRightHand && !bRightHandFilled;

				if (m_Player->GetRidingVehicleID().IsValid() && m_bAttemptInteractRightHand)
				{
					m_Player->SetRidingVehicle(nullptr);
				}
				else if (bPlaceItemLeft || bPlaceItemRight)
				{
					AttemptToIteractUsingHeldItem();
				}
				else if (bPickupItemLeft || bPickupItemRight)
				{
					AttemptToPickUpItem(nearbyInteractables);
				}
			}
		}

		if (m_bCancelPlaceItemFromInventory)
		{
			m_Player->m_bPreviewPlaceItemFromInventory = false;
		}

		if (m_Player->m_bPreviewPlaceItemFromInventory)
		{
			PreviewPlaceItemFromInventory();
		}

		if (m_bAttemptPlaceItemFromInventory)
		{
			m_bAttemptPlaceItemFromInventory = false;
			AttemptPlaceItemFromInventory();
		}

		if (m_bSpawnWire)
		{
			m_bSpawnWire = false;
			m_Player->SpawnWire();
		}

		if (m_bDropItem)
		{
			m_bDropItem = false;
			m_Player->DropSelectedItem();
		}

		if (m_Player->m_ObjectInteractingWithID.IsValid())
		{
			GameObject* objectInteractingWith = m_Player->m_ObjectInteractingWithID.Get();
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

			if (m_bAttemptInteractLeftHand || m_bAttemptInteractRightHand)
			{
				if (m_Player->m_TrackRidingID == InvalidTrackID)
				{
					real distAlongTrack = -1.0f;
					TrackID trackInRangeIndex = trackManager->GetTrackInRangeID(playerTransform->GetWorldPosition(), m_Player->m_TrackAttachMinDist, &distAlongTrack);
					if (trackInRangeIndex != InvalidTrackID)
					{
						m_bAttemptInteractLeftHand = false;
						m_bAttemptInteractRightHand = false;

						m_Player->AttachToTrack(trackInRangeIndex, distAlongTrack);

						SnapPosToTrack(m_Player->m_DistAlongTrack, false, m_Player->IsFacingDownTrack() ? -1.0f : 1.0f);
					}
				}
				else
				{
					m_bAttemptInteractLeftHand = false;
					m_bAttemptInteractRightHand = false;
					m_Player->DetachFromTrack();
				}
			}
		}

		bool bDrawLocalAxes = (m_Mode != Mode::FIRST_PERSON) && m_Player->IsVisible();
		if (bDrawLocalAxes)
		{
			const real lineLength = 4.0f;
			debugRenderer->drawLine(rbPos, rbPos + ToBtVec3(up) * lineLength, btVector3(0.0f, 1.0f, 0.0f));
			debugRenderer->drawLine(rbPos, rbPos + ToBtVec3(forward) * lineLength, btVector3(0.0f, 0.0f, 1.0f));
			debugRenderer->drawLine(rbPos, rbPos + ToBtVec3(right) * lineLength, btVector3(1.0f, 0.0f, 0.0f));
		}

		m_bAttemptCompleteTrack = false;
		m_bAttemptUndoTrackNode = false;
		m_bAttemptDeleteTrack = false;
		m_bAttemptTrackBuildingPrimary = false;
		m_bAttemptPlaceItemFromInventory = false;
		m_bAttemptInteractLeftHand = false;
		m_bAttemptInteractRightHand = false;
		m_bAttemptPickup = false;
	}

	void PlayerController::UpdatePreviewPlacementItem()
	{
		CHECK(m_Player->m_SelectedQuickAccessItemSlot >= 0 && m_Player->m_SelectedQuickAccessItemSlot < (i32)m_Player->m_QuickAccessInventory.size());
		GameObjectStack& gameObjectStack = m_Player->m_QuickAccessInventory[m_Player->m_SelectedQuickAccessItemSlot];

		if (gameObjectStack.count >= 1)
		{
			if (gameObjectStack.prefabID.IsValid())
			{
				UpdateItemPlacementTransform();

				real posInterpolateSpeed = 60.0f;
				real rotInterpolateSpeed = 25.0f;
				if (NearlyEquals(m_TargetItemPlacementPosSmoothed.y, m_TargetItemPlacementPos.y, 0.001f))
				{
					m_TargetItemPlacementPosSmoothed = m_TargetItemPlacementPos;
				}
				else
				{
					// Only interpolate position when changing height
					m_TargetItemPlacementPosSmoothed = MoveTowards(m_TargetItemPlacementPosSmoothed, m_TargetItemPlacementPos, posInterpolateSpeed);
				}
				m_TargetItemPlacementRotSmoothed = MoveTowards(m_TargetItemPlacementRotSmoothed, m_TargetItemPlacementRot, g_DeltaTime * rotInterpolateSpeed);

				GameObject* templateObject = g_ResourceManager->GetPrefabTemplate(gameObjectStack.prefabID);
				Mesh* mesh = templateObject->GetMesh();

				if (mesh != nullptr)
				{
					PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();

					if (m_ItemPlacementBoundingBoxShape == nullptr)
					{
						btVector3 boxHalfExtents = ToBtVec3((mesh->m_MaxPoint - mesh->m_MinPoint) * 0.5f);
						m_ItemPlacementBoundingBoxShape = new btBoxShape(boxHalfExtents);
					}

					{
						// Un-smoothed, un-snapped target pos
						glm::vec3 targetItemPlacementPos = GetTargetItemPos();
						real maxDrop = 5.0f;

						btTransform floatingTargetTransform(ToBtQuaternion(m_TargetItemPlacementRot), ToBtVec3(targetItemPlacementPos));

						btTransform sweepEndTransform(floatingTargetTransform.getRotation(), ToBtVec3(targetItemPlacementPos + (-VEC3_UP) * maxDrop));
						glm::vec3 groundPos, groundNormal;
						if (physicsWorld->GetPointOnGround(m_ItemPlacementBoundingBoxShape, floatingTargetTransform, sweepEndTransform, groundPos, groundNormal))
						{
							m_ItemPlacementGroundedPos.x = targetItemPlacementPos.x;
							m_ItemPlacementGroundedPos.y = groundPos.y +
								((mesh->m_MaxPoint.y - mesh->m_MinPoint.y) * 0.5f);
							m_ItemPlacementGroundedPos.z = targetItemPlacementPos.z;

							m_TargetItemPlacementPos = m_ItemPlacementGroundedPos;
						}
					}

					btTransform transform(ToBtQuaternion(m_TargetItemPlacementRot), ToBtVec3(m_TargetItemPlacementPos));

					btPairCachingGhostObject pairCache;
					pairCache.setCollisionShape(m_ItemPlacementBoundingBoxShape);
					pairCache.setWorldTransform(transform);
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

	void PlayerController::FixedUpdate()
	{
		Transform* playerTransform = m_Player->GetTransform();
		btRigidBody* rb = m_Player->GetRigidBody()->GetRigidBodyInternal();

		const real moveLR = -g_InputManager->GetActionAxisValue(Action::MOVE_LEFT) + g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);
		const real moveFB = -g_InputManager->GetActionAxisValue(Action::MOVE_BACKWARD) + g_InputManager->GetActionAxisValue(Action::MOVE_FORWARD);

		bool bMoving = false;

		TrackID pTrackRidingID = m_Player->m_TrackRidingID;
		bool bWasFacingDownTrack = m_Player->IsFacingDownTrack();

		const btVector3 vel = rb->getLinearVelocity();
		btVector3 xzVel(vel.getX(), 0, vel.getZ());
		real xzVelMagnitude = xzVel.length();
		if (xzVelMagnitude > m_MaxMoveSpeed)
		{
			// Limit maximum velocity
			if (m_MaxMoveSpeed > 1.0e-5f)
			{
				btVector3 xzVelNorm = xzVel.normalized();
				btVector3 newVel(xzVelNorm.getX() * m_MaxMoveSpeed, vel.getY(), xzVelNorm.getZ() * m_MaxMoveSpeed);
				rb->setLinearVelocity(newVel);
			}
			else
			{
				rb->setLinearVelocity(btVector3(0, 0, 0));
			}
		}

		m_Player->UpdateIsGrounded();

		if (m_Player->m_bPreviewPlaceItemFromInventory)
		{
			UpdatePreviewPlacementItem();
		}

		if (m_Player->m_bPossessed)
		{
			if (m_Player->m_TrackRidingID != InvalidTrackID)
			{
				TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

				glm::vec3 newCurveDir = trackManager->GetTrack(m_Player->m_TrackRidingID)->GetCurveDirectionAt(m_Player->m_DistAlongTrack);
				static glm::vec3 pCurveDir = newCurveDir;

				real move = moveFB;

				if (m_Player->IsFacingDownTrack())
				{
					move = -move;
				}

				// Facing forward down the track means facing towards decreasing t
				const real facingSign = m_Player->IsFacingDownTrack() ? -1.0f : 1.0f;
				const bool bMovingBackwards = (moveFB < 0.0f);
				const real travelSign = (move != 0.0f) ? glm::sign(move) : facingSign;

				real targetDDist = move * m_Player->m_TrackMoveSpeed * g_FixedDeltaTime;
				real pDist = m_Player->m_DistAlongTrack;
				m_Player->m_DistAlongTrack = trackManager->AdvanceTAlongTrack(m_Player->m_TrackRidingID,
					targetDDist, m_Player->m_DistAlongTrack);
				SnapPosToTrack(pDist, bMovingBackwards, travelSign);

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
				if (m_Player->AbleToInteract() &&
					!m_Player->IsAnyInventoryShowing())
				{
					glm::vec3 moveDir = playerTransform->GetRight() * moveLR + playerTransform->GetForward() * moveFB;
					moveDir.y = 0.0f;
					const real moveDirLen = glm::length(moveDir);
					if (moveDirLen > 1.0f)
					{
						moveDir /= moveDirLen;
					}

					bMoving = moveDirLen > 0.01f;
					if (bMoving)
					{
						// Drive horizontal velocity towards the target directly. Speeding up is gradual, while
						// slowing down/changing direction uses the (faster) deceleration rate
						const glm::vec3 currentVel = ToVec3(xzVel);
						const glm::vec3 targetVel = moveDir * m_MaxMoveSpeed;
						const real rate = glm::length(targetVel) > glm::length(currentVel) ? m_MoveAcceleration : m_MoveDeceleration;
						const glm::vec3 diff = targetVel - currentVel;
						const real diffLen = glm::length(diff);
						const real maxDelta = rate * g_FixedDeltaTime;
						const glm::vec3 newVel = (diffLen > maxDelta) ? (currentVel + diff * (maxDelta / diffLen)) : targetVel;
						rb->setLinearVelocity(btVector3(newVel.x, rb->getLinearVelocity().getY(), newVel.z));
						rb->activate();
					}
				}
			}
		}

		// Friction would fight the velocity set above, so it only applies when there's no move input (to stop quickly)
		rb->setFriction(bMoving ? 0.0f : m_Player->m_MoveFriction);
	}

	// Look is applied every frame rather than every fixed step so rotation isn't quantized to the physics rate.
	// Yaw is written straight through to the rigid body, which isn't simulated rotationally (see Player::PostInitialize)
	void PlayerController::UpdateLook()
	{
		if (!m_Player->AbleToInteract())
		{
			m_MouseLookAccum = VEC2_ZERO;
			m_PendingLook = VEC2_ZERO;
			return;
		}

		const bool bRidingTrack = (m_Player->m_TrackRidingID != InvalidTrackID);
		const real rotateHSpeed = (m_Mode == Mode::FIRST_PERSON ? m_RotateHSpeedFirstPerson : m_RotateHSpeedThirdPerson);

		// Mouse deltas are distances, so they aren't scaled by the frame time (g_FixedDeltaTime is only kept to preserve existing sensitivities)
		glm::vec2 lookInput(m_MouseLookAccum.x * m_MouseRotateHSpeed * rotateHSpeed * g_FixedDeltaTime, 0.0f);
		// Stick input is a rate. While riding, horizontal look input picks which way to go at junctions instead
		if (!bRidingTrack)
		{
			lookInput.x += (-g_InputManager->GetActionAxisValue(Action::LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::LOOK_RIGHT)) * rotateHSpeed * g_DeltaTime;
		}
		// Positive y looks down. In third person it tilts the overhead camera instead of the player
		lookInput.y += m_MouseLookAccum.y * m_MouseRotateVSpeed * (m_bInvertMouseV ? -1.0f : 1.0f) * m_RotateVSpeed * g_FixedDeltaTime;
		lookInput.y += (-g_InputManager->GetActionAxisValue(Action::LOOK_UP) + g_InputManager->GetActionAxisValue(Action::LOOK_DOWN)) * m_RotateVSpeed * g_DeltaTime;
		m_MouseLookAccum = VEC2_ZERO;

		m_PendingLook += lookInput;

		const real applyFrac = (m_LookSmoothingTime > 0.0f) ? (1.0f - glm::exp(-g_DeltaTime / m_LookSmoothingTime)) : 1.0f;
		const glm::vec2 look = m_PendingLook * applyFrac;
		m_PendingLook -= look;

		if (look.x != 0.0f)
		{
			if (bRidingTrack)
			{
				// The body follows the track, so look relative to it
				m_Player->m_RidingLookYaw = glm::mod(m_Player->m_RidingLookYaw + look.x + PI, TWO_PI) - PI;
			}
			else
			{
				Transform* playerTransform = m_Player->GetTransform();
				playerTransform->SetWorldRotation(glm::rotate(playerTransform->GetWorldRotation(), look.x, VEC3_UP));
			}
		}

		if (look.y != 0.0f)
		{
			if (m_Mode == Mode::FIRST_PERSON)
			{
				m_Player->AddToPitch(look.y);
			}
			else
			{
				BaseCamera* cam = g_CameraManager->CurrentCamera();
				if (cam->type == CameraType::OVERHEAD)
				{
					((OverheadCamera*)cam)->AddToElevation(look.y * m_CameraElevationSensitivity);
				}
			}
		}
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
		m_Player->GetRigidBody()->ResetInterpolation();
	}

	void PlayerController::DrawImGuiObjects()
	{
		const std::string treeName = "Player Controller " + IntToString(m_PlayerIndex);
		if (ImGui::TreeNode(treeName.c_str()))
		{
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

	void PlayerController::SnapPosToTrack(real pDistAlongTrack, bool bMovingBackwards, real travelSign)
	{
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
		TrackID newTrackID = m_Player->m_TrackRidingID;
		real newDistAlongTrack = m_Player->m_DistAlongTrack;

		// Pressing left/right picks a branch for the next fork, which sticks until a junction is passed so the key
		// doesn't need to be held at the exact moment of crossing. Directions are relative to the player, who faces
		// away from the direction of travel when moving backwards
		real steerInput = -g_InputManager->GetActionAxisValue(Action::MOVE_LEFT) + g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);
		if (steerInput > 0.5f)
		{
			m_ForkSteer = 1.0f;
		}
		else if (steerInput < -0.5f)
		{
			m_ForkSteer = -1.0f;
		}

		const real travelRelativeSteer = bMovingBackwards ? -m_ForkSteer : m_ForkSteer;
		LookDirection desiredDir = LookDirection::CENTER;
		if (travelRelativeSteer > 0.0f)
		{
			desiredDir = LookDirection::RIGHT;
		}
		else if (travelRelativeSteer < 0.0f)
		{
			desiredDir = LookDirection::LEFT;
		}

		trackManager->UpdatePreview(m_Player->m_TrackRidingID, m_Player->m_DistAlongTrack, desiredDir, travelSign, bMovingBackwards);

		i32 newJunctionIndex = -1;
		i32 newCurveIndex = -1;
		TrackState newTrackState = TrackState::_NONE;
		glm::vec3 newPos = trackManager->GetPointOnTrack(m_Player->m_TrackRidingID, m_Player->m_DistAlongTrack,
			pDistAlongTrack, desiredDir, bMovingBackwards, &newTrackID, &newDistAlongTrack,
			&newJunctionIndex, &newCurveIndex, &newTrackState, false);

		if (newJunctionIndex != -1)
		{
			// Passed through a junction, the choice has been used
			m_ForkSteer = 0.0f;
		}

		bool bSwitchedTracks = (newTrackID != InvalidTrackID) && (newTrackID != m_Player->m_TrackRidingID);
		if (bSwitchedTracks)
		{
			m_Player->m_TrackRidingID = newTrackID;
			m_Player->m_DistAlongTrack = newDistAlongTrack;

			if (newTrackState != TrackState::_NONE)
			{
				m_Player->m_TrackState = newTrackState;
			}
		}

		newPos += glm::vec3(0.0f, m_Player->m_Height / 2.0f, 0.0f);

		// Moving along the track is continuous, so keep interpolating between fixed steps (resetting it every step made riding jitter).
		// Large jumps (e.g. attaching to a track) are still treated as teleports
		RigidBody* rigidBody = m_Player->GetRigidBody();
		const glm::vec3 simPos = ToVec3(rigidBody->GetRigidBodyInternal()->getCenterOfMassPosition());
		const real teleportDist = 2.0f;
		rigidBody->SetWorldPosition(newPos, glm::distance2(simPos, newPos) > teleportDist * teleportDist);
	}

	void PlayerController::LoadConfigFile()
	{
		if (!m_ConfigFile.Deserialize())
		{
			// If file wasn't found we should write the defaults out now
			SerializeConfigFile();
		}
	}

	void PlayerController::SerializeConfigFile()
	{
		if (!m_ConfigFile.Serialize())
		{
			PrintError("Failed to serialize player settings file\n");
		}
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
			if (m_Player->m_bPreviewPlaceItemFromInventory)
			{
				m_bCancelPlaceItemFromInventory = true;
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::SHOW_INVENTORY &&
			!m_Player->m_bMinerInventoryShowing &&
			actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			m_Player->m_bInventoryShowing = !m_Player->m_bInventoryShowing;
			g_Window->SetCursorMode(m_Player->m_bInventoryShowing ? CursorMode::NORMAL : CursorMode::DISABLED);
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

		if (m_Player->IsAnyInventoryShowing())
		{
			return EventReply::UNCONSUMED;
		}

		if (action == Action::PLACE_ITEM && !m_bSpawnWire)
		{
			if (actionEvent == ActionEvent::ACTION_TRIGGER)
			{
				m_bItemPlacementValid = false;
				m_Player->m_bPreviewPlaceItemFromInventory = true;
				return EventReply::CONSUMED;
			}
			else if (actionEvent == ActionEvent::ACTION_RELEASE)
			{
				m_Player->m_bPreviewPlaceItemFromInventory = false;
				m_bAttemptPlaceItemFromInventory = true;
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::PLACE_WIRE && !m_Player->m_bPreviewPlaceItemFromInventory)
		{
			if (actionEvent == ActionEvent::ACTION_TRIGGER)
			{
				const bool bLeftHandFilled = m_Player->GetHeldItem(Hand::LEFT).IsValid();
				const bool bRightHandFilled = m_Player->GetHeldItem(Hand::RIGHT).IsValid();
				if (!bLeftHandFilled && !bRightHandFilled)
				{
					m_bSpawnWire = true;
					return EventReply::CONSUMED;
				}
				else
				{
					if (m_Player->GetHeldItem(Hand::LEFT).IsValid() &&
						m_Player->GetHeldItem(Hand::LEFT).Get()->GetTypeID() == WirePlugSID &&
						m_Player->GetHeldItem(Hand::RIGHT).IsValid() &&
						m_Player->GetHeldItem(Hand::RIGHT).Get()->GetTypeID() == WirePlugSID)
					{
						WirePlug* wirePlug0 = (WirePlug*)m_Player->GetHeldItem(Hand::LEFT).Get();
						Wire* wire = (Wire*)wirePlug0->wireID.Get();
						g_SceneManager->CurrentScene()->RemoveObject(m_Player->GetHeldItem(Hand::LEFT).Get(), true);
						g_SceneManager->CurrentScene()->RemoveObject(m_Player->GetHeldItem(Hand::RIGHT).Get(), true);
						g_SceneManager->CurrentScene()->RemoveObject(wire, true);
						m_Player->SetHeldItem(Hand::LEFT, InvalidGameObjectID);
						m_Player->SetHeldItem(Hand::RIGHT, InvalidGameObjectID);
						return EventReply::CONSUMED;
					}
				}
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
				m_Player->TogglePlacingTrack();
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::ENTER_TRACK_EDIT_MODE && actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			if (m_Player->m_TrackRidingID == InvalidTrackID)
			{
				m_Player->ToggleEditingTrack();
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::COMPLETE_TRACK && actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			if (m_Player->m_TrackRidingID == InvalidTrackID && m_Player->IsPlacingTrack())
			{
				m_bAttemptCompleteTrack = true;
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::DELETE_TRACK && actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			if (m_Player->m_TrackRidingID == InvalidTrackID && m_Player->IsEditingTrack())
			{
				m_bAttemptDeleteTrack = true;
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::UNDO_TRACK_NODE && actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			if (m_Player->m_TrackRidingID == InvalidTrackID && (m_Player->IsPlacingTrack() || m_Player->IsEditingTrack()))
			{
				m_bAttemptUndoTrackNode = true;
				return EventReply::CONSUMED;
			}
		}

		if (action == Action::PICKUP_ITEM)
		{
			if (actionEvent == ActionEvent::ACTION_TRIGGER &&
				m_Player->m_TrackRidingID == InvalidTrackID &&
				(m_Player->IsPlacingTrack() || m_Player->IsEditingTrack()))
			{
				// Pickup is also bound to a key, but only the mouse button should place track
				if (g_InputManager->IsMouseButtonDown(MouseButton::LEFT, true))
				{
					m_bAttemptTrackBuildingPrimary = true;
				}
				return EventReply::CONSUMED;
			}

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
				if (m_Player->m_ItemPickingTimer != -1.0f)
				{
					m_Player->SetItemPickingUp(nullptr);
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

		return EventReply::UNCONSUMED;
	}

	EventReply PlayerController::OnMouseMovedEvent(const glm::vec2& dMousePos)
	{
		if (m_Player != nullptr && !m_Player->IsAnyInventoryShowing() &&
			g_Window->HasFocus() && !g_EngineInstance->IsSimulationPaused())
		{
			if (m_Player->AbleToInteract())
			{
				m_MouseLookAccum += dMousePos * 0.01f;

				return EventReply::CONSUMED;
			}
		}

		return EventReply::UNCONSUMED;
	}

	void PlayerController::PreviewPlaceItemFromInventory()
	{
		CHECK(m_Player->m_SelectedQuickAccessItemSlot >= 0 && m_Player->m_SelectedQuickAccessItemSlot < (i32)m_Player->m_QuickAccessInventory.size());
		GameObjectStack& gameObjectStack = m_Player->m_QuickAccessInventory[m_Player->m_SelectedQuickAccessItemSlot];

		if (gameObjectStack.count >= 1)
		{
			if (gameObjectStack.prefabID.IsValid())
			{
				GameObject* templateObject = g_ResourceManager->GetPrefabTemplate(gameObjectStack.prefabID);
				if (!templateObject->IsItemizable())
				{
					m_Player->m_bPreviewPlaceItemFromInventory = false;
				}
				else
				{
					Mesh* mesh = templateObject->GetMesh();
					if (mesh != nullptr)
					{
						UpdateItemPlacementTransform();

						static const glm::vec4 validColour(2.0f, 4.0f, 2.5f, 0.4f);
						static const glm::vec4 invalidColour(4.0f, 2.0f, 2.0f, 0.4f);
						g_Renderer->QueueHologramMesh(gameObjectStack.prefabID,
							m_TargetItemPlacementPosSmoothed,
							m_TargetItemPlacementRotSmoothed,
							VEC3_ONE,
							m_bItemPlacementValid ? validColour : invalidColour);
					}
				}
			}
			else
			{
				std::string prefabIDStr = gameObjectStack.prefabID.ToString();
				PrintError("Failed to de-itemize item from player inventory, invalid prefab ID: %s\n", prefabIDStr.c_str());
			}
		}
	}

	void PlayerController::UpdateItemPlacementTransform()
	{
		if (m_ItemPlacementGroundedPos.x != FLT_MAX)
		{
			m_TargetItemPlacementPos = m_ItemPlacementGroundedPos;
		}
		else
		{
			m_TargetItemPlacementPos = GetTargetItemPos();
		}

		m_TargetItemPlacementRot = m_Player->GetTransform()->GetWorldRotation();

		if (m_TargetItemPlacementPosSmoothed.x == FLT_MAX)
		{
			m_TargetItemPlacementPosSmoothed = m_TargetItemPlacementPos;
			m_TargetItemPlacementRotSmoothed = m_TargetItemPlacementRot;
		}
	}

	glm::vec3 PlayerController::GetTargetItemPos()
	{
		real forwardOffset = 6.0f;
		real upOffset = 1.0f;

		Transform* playerTransform = m_Player->GetTransform();
		return playerTransform->GetWorldPosition() +
			playerTransform->GetForward() * forwardOffset +
			playerTransform->GetUp() * upOffset;
	}

	void PlayerController::AttemptPlaceItemFromInventory()
	{
		if (m_bItemPlacementValid)
		{
			CHECK(m_Player->m_SelectedQuickAccessItemSlot >= 0 && m_Player->m_SelectedQuickAccessItemSlot < (i32)m_Player->m_QuickAccessInventory.size());
			GameObjectStack& gameObjectStack = m_Player->m_QuickAccessInventory[m_Player->m_SelectedQuickAccessItemSlot];

			if (gameObjectStack.count >= 1)
			{
				if (gameObjectStack.prefabID.IsValid())
				{
					UpdateItemPlacementTransform();

					GameObject* gameObject = GameObject::Deitemize(gameObjectStack.prefabID, m_TargetItemPlacementPos, m_TargetItemPlacementRot, gameObjectStack.userData);

					m_TargetItemPlacementPosSmoothed = glm::vec3(FLT_MAX);
					m_TargetItemPlacementRotSmoothed = glm::quat(glm::vec3(FLT_MAX));
					m_ItemPlacementGroundedPos = glm::vec3(FLT_MAX);

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

	void PlayerController::AttemptToIteractUsingHeldItem()
	{
		bool bUseItemLeft = m_bAttemptInteractLeftHand && m_Player->GetHeldItem(Hand::LEFT).IsValid();
		bool bUseItemRight = m_bAttemptInteractRightHand && m_Player->GetHeldItem(Hand::RIGHT).IsValid();

		GameObject* heldItemLeftHand = m_Player->GetHeldItem(Hand::LEFT).Get();
		GameObject* heldItemRightHand = m_Player->GetHeldItem(Hand::RIGHT).Get();
		bool bPlaceWireLeft = bUseItemLeft && heldItemLeftHand->GetTypeID() == WirePlugSID;
		bool bPlaceWireRight = bUseItemRight && heldItemRightHand->GetTypeID() == WirePlugSID;
		if (bPlaceWireLeft || bPlaceWireRight)
		{
			WirePlug* wirePlug = (WirePlug*)(bPlaceWireLeft ? heldItemLeftHand : heldItemRightHand);
			PluggablesSystem* pluggablesSystem = GetSystem<PluggablesSystem>(SystemType::PLUGGABLES);
			if (pluggablesSystem->PlugInToNearbySocket(wirePlug, WirePlug::nearbyThreshold))
			{
				if (bPlaceWireLeft)
				{
					m_Player->SetHeldItem(Hand::LEFT, InvalidGameObjectID);
					m_bAttemptInteractLeftHand = false;
				}
				else
				{
					m_Player->SetHeldItem(Hand::RIGHT, InvalidGameObjectID);
					m_bAttemptInteractRightHand = false;
				}
			}
		}
	}

	void PlayerController::AttemptToPickUpItem(std::list<Pair<GameObject*, real>>& nearbyInteractables)
	{
		auto nearbyInteractableIter = nearbyInteractables.begin();
		bool bInteracted = false;
		// TODO: Run this loop every frame to indicate to player what each hand will interact with
		while (!bInteracted && nearbyInteractableIter != nearbyInteractables.end())
		{
			GameObject* nearbyInteractable = nearbyInteractableIter->first;

			bool bPickupItemLeft = m_bAttemptInteractLeftHand && !m_Player->GetHeldItem(Hand::LEFT).IsValid();
			bool bPickupItemRight = m_bAttemptInteractRightHand && !m_Player->GetHeldItem(Hand::RIGHT).IsValid();

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
							m_Player->SetHeldItem(Hand::LEFT, wirePlugInteractingWith->ID);
							m_bAttemptInteractLeftHand = false;
						}
						else if (bPickupItemRight)
						{
							m_Player->SetHeldItem(Hand::RIGHT, wirePlugInteractingWith->ID);
							m_bAttemptInteractRightHand = false;
						}
					}
					else
					{
						// Plug is not plugged in, grab plug
						bInteracted = true;
						if (bPickupItemLeft)
						{
							m_Player->SetHeldItem(Hand::LEFT, wirePlugInteractingWith->ID);
							m_bAttemptInteractLeftHand = false;
						}
						else if (bPickupItemRight)
						{
							m_Player->SetHeldItem(Hand::RIGHT, wirePlugInteractingWith->ID);
							m_bAttemptInteractRightHand = false;
						}
					}
				}
			} break;
			case TerminalSID:
			{
				Terminal* terminal = (Terminal*)nearbyInteractable;
				m_Player->SetInteractingWithTerminal(terminal);
				m_bAttemptInteractLeftHand = false;
				m_bAttemptInteractRightHand = false;
				bInteracted = true;
			} break;
			case SpeakerSID:
			{
				Speaker* speaker = (Speaker*)nearbyInteractable;
				speaker->TogglePlaying();
				m_bAttemptInteractLeftHand = false;
				m_bAttemptInteractRightHand = false;
				bInteracted = true;
			} break;
			case MinerSID:
			{
				if (!m_Player->m_bInventoryShowing)
				{
					m_Player->m_bMinerInventoryShowing = !m_Player->m_bMinerInventoryShowing;
					m_bAttemptInteractLeftHand = false;
					m_bAttemptInteractRightHand = false;
					bInteracted = true;
				}
			} break;
			case VehicleSID:
			{
				if (!m_Player->GetRidingVehicleID().IsValid())
				{
					Vehicle* vehicle = (Vehicle*)nearbyInteractable;
					m_Player->SetRidingVehicle(vehicle);
					m_bAttemptInteractLeftHand = false;
					m_bAttemptInteractRightHand = false;
					bInteracted = true;
				}
			} break;
			}

			++nearbyInteractableIter;
		}
	}
} // namespace flex
