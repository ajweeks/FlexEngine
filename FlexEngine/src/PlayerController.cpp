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
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsTypeConversions.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	PlayerController::PlayerController() :
		m_ActionCallback(this, &PlayerController::OnActionEvent)
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
	}

	void PlayerController::Destroy()
	{
		g_InputManager->UnbindActionCallback(&m_ActionCallback);
	}

	void PlayerController::Update()
	{
		// TODO: Make frame-rate-independent!

		btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();

		const real moveLR = g_InputManager->GetActionAxisValue(Action::MOVE_LEFT) + g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);
		const real moveFB = g_InputManager->GetActionAxisValue(Action::MOVE_FORWARD) + g_InputManager->GetActionAxisValue(Action::MOVE_BACKWARD);
		const real lookLR = g_InputManager->GetActionAxisValue(Action::LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::LOOK_RIGHT);
		//const real lookUD = g_InputManager->GetActionAxisValue(Action::LOOK_UP) + g_InputManager->GetActionAxisValue(Action::LOOK_DOWN);

		if (m_Player->m_bPossessed)
		{
			if (m_Player->m_TrackRidingID == InvalidTrackID)
			{
				TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();

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
					for (const BezierCurve& curve : m_Player->m_TrackPlacing.curves)
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
						for (const BezierCurve& curve : trackEditing->curves)
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

				GameObject* nearestInteractable = nullptr;
				if (m_Player->m_ObjectInteractingWith == nullptr)
				{
					std::vector<GameObject*> allInteractableObjects;
					g_SceneManager->CurrentScene()->GetInteractableObjects(allInteractableObjects);

					if (!allInteractableObjects.empty())
					{
						// List of objects sorted by sqr dist (closest to farthest)
						std::list<Pair<GameObject*, real>> nearbyInteractables;

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
									break;
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
									nearestInteractable->SetNearbyInteractable(true);
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
					}
				}
			}

			if (!m_Player->m_Inventory.empty())
			{
				// Place item from inventory
				if (m_bAttemptPlaceItemFromInventory)
				{
					m_bAttemptPlaceItemFromInventory = false;

					GameObject* obj = m_Player->m_Inventory[0];
					bool bPlaced = false;

					if (EngineCart* engineCart = dynamic_cast<EngineCart*>(obj))
					{
						TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
						glm::vec3 samplePos = m_Player->m_Transform.GetWorldPosition() + m_Player->m_Transform.GetForward() * 1.5f;
						real rangeThreshold = 4.0f;
						real distAlongNearestTrack;
						TrackID nearestTrackID = trackManager->GetTrackInRangeID(samplePos, rangeThreshold, &distAlongNearestTrack);

						if (nearestTrackID != InvalidTrackID)
						{
							bPlaced = true;
							engineCart->OnTrackMount(nearestTrackID, distAlongNearestTrack);
							engineCart->SetVisible(true);
						}
					}
					else if (Cart* cart = dynamic_cast<Cart*>(obj))
					{
						TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
						glm::vec3 samplePos = m_Player->m_Transform.GetWorldPosition() + m_Player->m_Transform.GetForward() * 1.5f;
						real rangeThreshold = 4.0f;
						real distAlongNearestTrack;
						TrackID nearestTrackID = trackManager->GetTrackInRangeID(samplePos, rangeThreshold, &distAlongNearestTrack);

						if (nearestTrackID != InvalidTrackID)
						{
							bPlaced = true;
							cart->OnTrackMount(nearestTrackID, distAlongNearestTrack);
							cart->SetVisible(true);
						}
					}
					else if (MobileLiquidBox* box = dynamic_cast<MobileLiquidBox*>(obj))
					{
						std::vector<Cart*> carts = g_SceneManager->CurrentScene()->GetObjectsOfType<Cart>();
						glm::vec3 playerPos = m_Player->m_Transform.GetWorldPosition();
						real threshold = 8.0f;
						real closestCartDist = threshold;
						i32 closestCartIdx = -1;
						for (i32 i = 0; i < (i32)carts.size(); ++i)
						{
							real d = glm::distance(carts[i]->GetTransform()->GetWorldPosition(), playerPos);
							if (d <= closestCartDist)
							{
								closestCartDist = d;
								closestCartIdx = i;
							}
						}

						if (closestCartIdx != -1)
						{
							if (box->GetParent() == nullptr)
							{
								g_SceneManager->CurrentScene()->RemoveRootObject(box, false);
							}

							bPlaced = true;

							carts[closestCartIdx]->AddChild(box);
							box->GetTransform()->SetLocalPosition(glm::vec3(0.0f, 1.5f, 0.0f));
							box->SetVisible(true);
						}
					}
					else
					{
						PrintWarn("Unhandled object in inventory attempted to be placed! %s\n", obj->GetName().c_str());
					}

					if (bPlaced)
					{
						m_Player->m_Inventory.erase(m_Player->m_Inventory.begin());
					}
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
		TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
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
			else if (!m_Player->GetObjectInteractingWith())
			{
				force += ToBtVec3(transform->GetRight()) * m_MoveAcceleration * moveLR;
				force += ToBtVec3(transform->GetForward()) * m_MoveAcceleration * moveFB;
			}
		}

		bool bDrawLocalAxes = (m_Mode != Mode::FIRST_PERSON);
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
			xzVelMagnitude = m_MaxMoveSpeed;
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
			!m_Player->IsBeingInteractedWith() &&
			m_Player->m_TrackRidingID == InvalidTrackID)
		{
			real lookH = lookLR;
			if (m_Player->IsRidingTrack())
			{
				lookH += g_InputManager->GetActionAxisValue(Action::MOVE_LEFT) + g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);
			}
			real lookV = 0.0f;
			if (m_Mode == Mode::FIRST_PERSON)
			{
				lookV = g_InputManager->GetActionAxisValue(Action::LOOK_DOWN) + g_InputManager->GetActionAxisValue(Action::LOOK_UP);
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
		TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
		TrackID newTrackID = m_Player->m_TrackRidingID;
		real newDistAlongTrack = m_Player->m_DistAlongTrack;
		LookDirection desiredDir = LookDirection::CENTER;

		const real moveLR = g_InputManager->GetActionAxisValue(Action::MOVE_LEFT) + g_InputManager->GetActionAxisValue(Action::MOVE_RIGHT);
		const real lookLR = g_InputManager->GetActionAxisValue(Action::LOOK_LEFT) + g_InputManager->GetActionAxisValue(Action::LOOK_RIGHT);

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

	EventReply PlayerController::OnActionEvent(Action action)
	{
		if (m_Player->m_bPossessed)
		{
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

			if (action == Action::TOGGLE_TABLET)
			{
				m_Player->m_bTabletUp = !m_Player->m_bTabletUp;
				return EventReply::CONSUMED;
			}

			if (action == Action::PLACE_ITEM)
			{
				if (!m_Player->m_Inventory.empty())
				{
					m_bAttemptPlaceItemFromInventory = true;
					return EventReply::CONSUMED;
				}
			}

			if (action == Action::INTERACT)
			{
				m_bAttemptInteract = true;
				// TODO: Determine if we can interact with anything here to allow
				// others to consume this event in the case we don't want it
				return EventReply::CONSUMED;
			}

			// TODO: Set flags here and move "real" code to Update?
			if (action == Action::DBG_ADD_CART_TO_INV)
			{
				BaseScene* scene = g_SceneManager->CurrentScene();
				CartID cartID = scene->GetCartManager()->CreateCart(scene->GetUniqueObjectName("Cart_", 2));
				Cart* cart = scene->GetCartManager()->GetCart(cartID);
				cart->SetVisible(false);
				m_Player->m_Inventory.push_back(cart);
				return EventReply::CONSUMED;
			}

			if (action == Action::DBG_ADD_ENGINE_CART_TO_INV)
			{
				BaseScene* scene = g_SceneManager->CurrentScene();
				CartID cartID = scene->GetCartManager()->CreateEngineCart(scene->GetUniqueObjectName("EngineCart_", 2));
				EngineCart* engineCart = static_cast<EngineCart*>(scene->GetCartManager()->GetCart(cartID));
				engineCart->SetVisible(false);
				m_Player->m_Inventory.push_back(engineCart);
				return EventReply::CONSUMED;
			}

			if (action == Action::DBG_ADD_LIQUID_BOX_TO_INV)
			{
				MobileLiquidBox* box = new MobileLiquidBox();
				g_SceneManager->CurrentScene()->AddObjectAtEndOFFrame(box);
				box->SetVisible(false);
				m_Player->m_Inventory.push_back(box);
				return EventReply::CONSUMED;
			}
		}

		return EventReply::UNCONSUMED;
	}

} // namespace flex
