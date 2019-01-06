#include "stdafx.hpp"

#include "Player.hpp"

#pragma warning(push, 0)
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <LinearMath/btIDebugDraw.h>

#include <glm/gtx/rotate_vector.hpp>
#pragma warning(pop)

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/FirstPersonCamera.hpp"
#include "Cameras/OverheadCamera.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	Player::Player(i32 index, const glm::vec3& initialPos /* = VEC3_ZERO */) :
		GameObject("Player " + std::to_string(index), GameObjectType::PLAYER),
		m_Index(index),
		m_TrackPlacementReticlePos(0.0f, -1.95f, 3.5f)
	{
		m_Transform.SetWorldPosition(initialPos);
	}

	Player::~Player()
	{
	}

	void Player::Initialize()
	{
		m_SoundPlaceTrackNodeID = AudioManager::AddAudioSource(RESOURCE_LOCATION "audio/click-02.wav");
		m_SoundPlaceFinalTrackNodeID = AudioManager::AddAudioSource(RESOURCE_LOCATION "audio/jingle-single-01.wav");
		m_SoundTrackAttachID = AudioManager::AddAudioSource(RESOURCE_LOCATION "audio/crunch-13.wav");
		m_SoundTrackDetachID = AudioManager::AddAudioSource(RESOURCE_LOCATION "audio/schluck-02.wav");
		m_SoundTrackSwitchDirID = AudioManager::AddAudioSource(RESOURCE_LOCATION "audio/whistle-01.wav");
		//m_SoundTrackAttachID = AudioManager::AddAudioSource(RESOURCE_LOCATION  "audio/schluck-07.wav");

		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "Player " + std::to_string(m_Index) + " material";
		matCreateInfo.shaderName = "pbr";
		matCreateInfo.constAlbedo = glm::vec3(0.89f, 0.93f, 0.98f);
		matCreateInfo.constMetallic = 0.0f;
		matCreateInfo.constRoughness = 0.98f;
		matCreateInfo.constAO = 1.0f;
		MaterialID matID = g_Renderer->InitializeMaterial(&matCreateInfo);

		RigidBody* rigidBody = new RigidBody();
		rigidBody->SetAngularDamping(0.1f);
		rigidBody->SetLinearDamping(0.1f);
		rigidBody->SetFriction(m_MoveFriction);

		btCapsuleShape* collisionShape = new btCapsuleShape(1.0f, 2.0f);

		m_MeshComponent = new MeshComponent(matID, this);
		AddTag("Player" + std::to_string(m_Index));
		SetRigidBody(rigidBody);
		SetStatic(false);
		SetSerializable(false);
		SetCollisionShape(collisionShape);
		m_MeshComponent->LoadFromFile(RESOURCE_LOCATION  "meshes/capsule.glb");

		m_Controller = new PlayerController();
		m_Controller->Initialize(this);

		// Map tablet
		{
			MaterialCreateInfo mapTabletMatCreateInfo = {};
			mapTabletMatCreateInfo.name = "Map tablet material";
			mapTabletMatCreateInfo.shaderName = "pbr";
			mapTabletMatCreateInfo.constAlbedo = glm::vec3(0.34f, 0.38f, 0.39f);
			mapTabletMatCreateInfo.constMetallic = 1.0f;
			mapTabletMatCreateInfo.constRoughness = 0.24f;
			mapTabletMatCreateInfo.constAO = 1.0f;
			MaterialID mapTabletMatID = g_Renderer->InitializeMaterial(&mapTabletMatCreateInfo);

			m_MapTabletHolder = new GameObject("Map tablet", GameObjectType::NONE);
			m_TabletOrbitAngle = m_TabletOrbitAngleUp;
			m_bTabletUp = true;
			m_MapTabletHolder->GetTransform()->SetLocalRotation(glm::quat(glm::vec3(0.0f, m_TabletOrbitAngle, 0.0f)));
			AddChild(m_MapTabletHolder);

			m_MapTablet = new GameObject("Map tablet mesh", GameObjectType::NONE);
			//RigidBody* tabletRB = m_MapTablet->SetRigidBody(new RigidBody());
			//tabletRB->SetKinematic(true);
			//tabletRB->SetLocalRotation(glm::quat(glm::vec3(glm::radians(90.0f), 0.0f, 0.0f)));
			//m_MapTablet->SetCollisionShape(new btBoxShape(btVector3(0.45f, 0.68f, 0.08f)));
			MeshComponent* mapTabletMesh = m_MapTablet->SetMeshComponent(new MeshComponent(mapTabletMatID, m_MapTablet));
			mapTabletMesh->LoadFromFile(RESOURCE_LOCATION  "meshes/map_tablet.glb");
			m_MapTabletHolder->AddChild(m_MapTablet);
			m_MapTablet->GetTransform()->SetLocalPosition(glm::vec3(-0.75f, -0.3f, 2.3f));
			m_MapTablet->GetTransform()->SetLocalRotation(glm::quat(glm::vec3(-glm::radians(80.0f), glm::radians(13.3f), -glm::radians(86.0f))));
		}

		m_CrosshairTextureID = g_Renderer->InitializeTexture(RESOURCE_LOCATION  "textures/cross-hair-01.png", 4, false, false, false);

		GameObject::Initialize();
	}

	void Player::PostInitialize()
	{
		m_RigidBody->SetOrientationConstraint(btVector3(0.0f, 1.0f, 0.0f));
		m_RigidBody->GetRigidBodyInternal()->setSleepingThresholds(0.0f, 0.0f);

		GameObject::PostInitialize();
	}

	void Player::Destroy()
	{
		if (m_Controller)
		{
			m_Controller->Destroy();
			SafeDelete(m_Controller);
		}

		AudioManager::DestroyAudioSource(m_SoundPlaceTrackNodeID);
		AudioManager::DestroyAudioSource(m_SoundPlaceFinalTrackNodeID);
		AudioManager::DestroyAudioSource(m_SoundTrackAttachID);
		AudioManager::DestroyAudioSource(m_SoundTrackDetachID);
		AudioManager::DestroyAudioSource(m_SoundTrackSwitchDirID);

		GameObject::Destroy();
	}

	void Player::Update()
	{
		if (g_InputManager->IsGamepadButtonPressed(m_Index, Input::GamepadButton::X))
		{
			Player* p = (Player*)this;

			if (p->m_ObjectInteractingWith)
			{
				// Toggle interaction when already interacting
				p->m_ObjectInteractingWith->SetInteractingWith(nullptr);
				p->SetInteractingWith(nullptr);
			}
			else
			{
				std::vector<GameObject*> interactibleObjects;
				g_SceneManager->CurrentScene()->GetInteractibleObjects(interactibleObjects);

				if (interactibleObjects.empty())
				{
					p->SetInteractingWith(nullptr);
				}
				else
				{
					GameObject* interactibleObj = nullptr;

					for (GameObject* obj : interactibleObjects)
					{
						if (Find(overlappingObjects, obj) != overlappingObjects.end())
						{
							interactibleObj = obj;
						}
					}

					if (interactibleObj)
					{
						GameObject* objInteractingWith = interactibleObj->GetObjectInteractingWith();
						if (objInteractingWith == nullptr)
						{
							interactibleObj->SetInteractingWith(p);
							p->SetInteractingWith(interactibleObj);
						}
					}
				}
			}
		}

		if (g_InputManager->IsGamepadButtonPressed(m_Index, Input::GamepadButton::A) ||
			(g_InputManager->bPlayerUsingKeyboard[m_Index] && g_InputManager->GetKeyPressed(Input::KeyCode::KEY_E)))
		{
			m_bTabletUp = !m_bTabletUp;
		}


		if (g_InputManager->GetKeyPressed(Input::KeyCode::KEY_C))
		{
			Cart* cart = new Cart();
			cart->Initialize();
			cart->PostInitialize();
			g_SceneManager->CurrentScene()->AddObjectAtEndOFFrame(cart);
			m_Inventory.push_back(cart);
		}

		if (!m_Inventory.empty())
		{
			// Place item in inventory
			if (g_InputManager->HasGamepadAxisValueJustPassedThreshold(m_Index, Input::GamepadAxis::RIGHT_TRIGGER, 0.5f) ||
				(g_InputManager->bPlayerUsingKeyboard[m_Index] && g_InputManager->GetKeyPressed(Input::KeyCode::KEY_SPACE)))
			{
				GameObject* obj = m_Inventory[0];
				bool bPlaced = false;

				if (Cart* objCart = (Cart*)obj)
				{
					TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
					glm::vec3 samplePos = m_Transform.GetWorldPosition() + m_Transform.GetForward() * 1.5f;
					real rangeThreshold = 10.0f;
					real distAlongNearestTrack;
					TrackID nearestTrackID = trackManager->GetTrackInRangeID(samplePos, rangeThreshold, &distAlongNearestTrack);

					if (nearestTrackID != InvalidTrackID)
					{
						bPlaced = true;
						objCart->OnTrackMount(nearestTrackID, distAlongNearestTrack);
					}
				}
				else
				{
					PrintWarn("Unhandled object in inventory attempted to be placed! %s\n", obj->GetName().c_str());
				}

				if (bPlaced)
				{
					m_Inventory.erase(m_Inventory.begin());
				}
			}
		}

		m_Controller->Update();

		TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();

		if (m_TrackRidingID != InvalidTrackID)
		{
			glm::vec3 trackForward = trackManager->GetTrack(m_TrackRidingID)->GetCurveDirectionAt(m_DistAlongTrack);
			real invTurnSpeed = m_TurnToFaceDownTrackInvSpeed;
			if (m_TrackState == TrackState::FACING_FORWARD)
			{
				trackForward = -trackForward;
			}
			glm::quat desiredRot = glm::quatLookAt(trackForward, m_Transform.GetUp());
			glm::quat rot = glm::slerp(m_Transform.GetWorldRotation(), desiredRot, 1.0f - glm::clamp(g_DeltaTime * invTurnSpeed, 0.0f, 0.99f));
			m_Transform.SetWorldRotation(rot, true);
		}

		SpriteQuadDrawInfo drawInfo = {};
		drawInfo.anchor = AnchorPoint::CENTER;
		drawInfo.bScreenSpace = true;
		drawInfo.bWriteDepth = false;
		drawInfo.bReadDepth = false;
		drawInfo.scale = glm::vec3(0.02f);
		drawInfo.textureHandleID = g_Renderer->GetTextureHandle(m_CrosshairTextureID);
		g_Renderer->DrawSprite(drawInfo);

		if (m_bPossessed &&
			m_TrackRidingID == InvalidTrackID)
		{
			if (g_InputManager->IsGamepadButtonPressed(m_Index, Input::GamepadButton::Y) ||
				(g_InputManager->bPlayerUsingKeyboard[m_Index] && g_InputManager->GetKeyPressed(Input::KeyCode::KEY_Q)))
			{
				m_bPlacingTrack = !m_bPlacingTrack;
				m_bEditingTrack = false;
			}

			if (g_InputManager->IsGamepadButtonPressed(m_Index, Input::GamepadButton::B) ||
				(g_InputManager->bPlayerUsingKeyboard[m_Index] && g_InputManager->GetKeyPressed(Input::KeyCode::KEY_F)))
			{
				m_bEditingTrack = !m_bEditingTrack;
				m_bPlacingTrack = false;
			}
		}

		if (m_bPossessed &&
			m_bPlacingTrack &&
			m_TrackRidingID == InvalidTrackID)
		{
			glm::vec3 reticlePos = GetTrackPlacementReticlePosWS(1.0f);

			if (g_InputManager->HasGamepadAxisValueJustPassedThreshold(m_Index, Input::GamepadAxis::RIGHT_TRIGGER, 0.5f) ||
				(g_InputManager->bPlayerUsingKeyboard[m_Index] && g_InputManager->GetKeyPressed(Input::KeyCode::KEY_X)))
			{
				m_CurvePlacing.points[m_CurveNodesPlaced++] = reticlePos;
				if (m_CurveNodesPlaced == 4)
				{
					AudioManager::PlaySource(m_SoundPlaceFinalTrackNodeID);

					m_CurvePlacing.CalculateLength();
					m_TrackPlacing.curves.push_back(m_CurvePlacing);

					glm::vec3 prevHandlePos = m_CurvePlacing.points[2];

					m_CurveNodesPlaced = 0;
					m_CurvePlacing.points[0] = m_CurvePlacing.points[1] = m_CurvePlacing.points[2] = m_CurvePlacing.points[3] = VEC3_ZERO;

					glm::vec3 controlPointPos = reticlePos;
					glm::vec3 nextHandlePos = controlPointPos + (controlPointPos - prevHandlePos);
					m_CurvePlacing.points[m_CurveNodesPlaced++] = controlPointPos;
					m_CurvePlacing.points[m_CurveNodesPlaced++] = nextHandlePos;
				}
				else
				{
					AudioManager::PlaySource(m_SoundPlaceTrackNodeID);
				}

				for (i32 i = 3; i > m_CurveNodesPlaced - 1; --i)
				{
					m_CurvePlacing.points[i] = reticlePos;
				}
			}

			if (g_InputManager->HasGamepadAxisValueJustPassedThreshold(m_Index, Input::GamepadAxis::LEFT_TRIGGER, 0.5f) ||
				(g_InputManager->bPlayerUsingKeyboard[m_Index] && g_InputManager->GetKeyPressed(Input::KeyCode::KEY_C)))
			{
				m_CurveNodesPlaced = 0;
				m_CurvePlacing.points[0] = m_CurvePlacing.points[1] = m_CurvePlacing.points[2] = m_CurvePlacing.points[3] = VEC3_ZERO;

				if (!m_TrackPlacing.curves.empty())
				{
					trackManager->AddTrack(m_TrackPlacing);
					trackManager->FindJunctions();
					m_TrackPlacing.curves.clear();
				}
			}

			static const btVector4 placedCurveCol(0.5f, 0.8f, 0.3f, 0.9f);
			static const btVector4 placingCurveCol(0.35f, 0.6f, 0.3f, 0.9f);
			for (const BezierCurve& curve : m_TrackPlacing.curves)
			{
				curve.DrawDebug(false, placedCurveCol, placedCurveCol);

			}
			if (m_CurveNodesPlaced > 0)
			{
				m_CurvePlacing.DrawDebug(false, placingCurveCol, placingCurveCol);
			}

			btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
			btTransform cylinderTransform(ToBtQuaternion(m_Transform.GetWorldRotation()), ToBtVec3(reticlePos));
			debugDrawer->drawCylinder(0.6f, 0.001f, 1, cylinderTransform, btVector3(0.18f, 0.22f, 0.35f));
			debugDrawer->drawCylinder(1.1f, 0.001f, 1, cylinderTransform, btVector3(0.18f, 0.22f, 0.35f));
		}

		if (m_bPossessed &&
			m_bEditingTrack &&
			m_TrackRidingID == InvalidTrackID)
		{
			// TODO: Snap to points other than the one we are editing
			glm::vec3 reticlePos = GetTrackPlacementReticlePosWS(m_TrackEditingID == InvalidTrackID ? 1.0f : 0.0f, true);
			if (g_InputManager->HasGamepadAxisValueJustPassedThreshold(m_Index, Input::GamepadAxis::RIGHT_TRIGGER, 0.5f) ||
				(g_InputManager->bPlayerUsingKeyboard[m_Index] && g_InputManager->GetKeyPressed(Input::KeyCode::KEY_X)))
			{
				if (m_TrackEditingCurveIdx == -1)
				{
					TrackID trackID = InvalidTrackID;
					i32 curveIndex = -1;
					i32 pointIndex = -1;
					if (trackManager->GetPointInRange(reticlePos, 0.1f, &trackID, &curveIndex, &pointIndex))
					{
						m_TrackEditingID = trackID;
						m_TrackEditingCurveIdx = curveIndex;
						m_TrackEditingPointIdx = pointIndex;
					}
				}
				else
				{
					m_TrackEditingID = InvalidTrackID;
					m_TrackEditingCurveIdx = -1;
					m_TrackEditingPointIdx = -1;
				}
			}

			if (m_TrackEditingID != InvalidTrackID)
			{
				BezierCurveList* trackEditing = trackManager->GetTrack(m_TrackEditingID);
				glm::vec3 point = trackEditing->GetPointOnCurve(m_TrackEditingCurveIdx, m_TrackEditingPointIdx);

				glm::vec3 newPoint(reticlePos.x, point.y, reticlePos.z);
				trackEditing->SetPointPosAtIndex(m_TrackEditingCurveIdx, m_TrackEditingPointIdx, newPoint, true);

				static const btVector4 editedCurveCol(0.3f, 0.85f, 0.53f, 0.9f);
				static const btVector4 editingCurveCol(0.2f, 0.8f, 0.25f, 0.9f);
				for (const BezierCurve& curve : trackEditing->curves)
				{
					curve.DrawDebug(false, editedCurveCol, editingCurveCol);
				}

				trackManager->FindJunctions();
			}

			btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();
			btTransform cylinderTransform(ToBtQuaternion(m_Transform.GetWorldRotation()), ToBtVec3(reticlePos));
			static btVector3 ringColEditing(0.48f, 0.22f, 0.65f);
			static btVector3 ringColEditingActive(0.6f, 0.4f, 0.85f);
			debugDrawer->drawCylinder(0.6f, 0.001f, 1, cylinderTransform, m_TrackEditingID == InvalidTrackID ? ringColEditing : ringColEditingActive);
			debugDrawer->drawCylinder(1.1f, 0.001f, 1, cylinderTransform, m_TrackEditingID == InvalidTrackID ? ringColEditing : ringColEditingActive);
		}

		if (m_bTabletUp)
		{
			m_TabletOrbitAngle = MoveTowards(m_TabletOrbitAngle, m_TabletOrbitAngleUp, g_DeltaTime * 10.0f);
		}
		else
		{
			m_TabletOrbitAngle = MoveTowards(m_TabletOrbitAngle, m_TabletOrbitAngleDown, g_DeltaTime * 10.0f);
		}
		m_MapTabletHolder->GetTransform()->SetLocalRotation(glm::quat(glm::vec3(0.0f, glm::radians(m_TabletOrbitAngle), 0.0f)));

		GameObject::Update();
	}

	void Player::SetPitch(real pitch)
	{
		m_Pitch = pitch;
		ClampPitch();
	}

	void Player::AddToPitch(real deltaPitch)
	{
		m_Pitch += deltaPitch;
		ClampPitch();
	}

	real Player::GetPitch() const
	{
		return m_Pitch;
	}

	glm::vec3 Player::GetLookDirection() const
	{
		glm::mat4 rotMat = glm::mat4(m_Transform.GetWorldRotation());
		glm::vec3 lookDir = rotMat[2];
		lookDir = glm::rotate(lookDir, m_Pitch, m_Transform.GetRight());

		return lookDir;
	}

	i32 Player::GetIndex() const
	{
		return m_Index;
	}

	real Player::GetHeight() const
	{
		return m_Height;
	}

	PlayerController* Player::GetController()
	{
		return m_Controller;
	}

	void Player::DrawImGuiObjects()
	{
		std::string treeNodeName = "Player " + IntToString(m_Index);
		if (ImGui::TreeNode(treeNodeName.c_str()))
		{
			ImGui::Text("Pitch: %.2f", GetPitch());
			glm::vec3 euler = glm::eulerAngles(GetTransform()->GetWorldRotation());
			ImGui::Text("World rot: %.2f, %.2f, %.2f", euler.x, euler.y, euler.z);

			bool bRiding = (m_TrackRidingID != InvalidTrackID);
			ImGui::Text("Riding track: %s", (bRiding ? "true" : "false"));
			if (bRiding)
			{
				ImGui::Indent();
				ImGui::Text("Dist along track: %.2f", GetDistAlongTrack());
				ImGui::Text("Track state: %s", (TrackStateToString(m_TrackState)));
				ImGui::Unindent();
			}

			ImGui::TreePop();
		}
	}

	void Player::ClampPitch()
	{
		real limit = glm::radians(89.5f);
		m_Pitch = glm::clamp(m_Pitch, -limit, limit);
	}

	void Player::UpdateIsGrounded()
	{
		btVector3 rayStart = ToBtVec3(m_Transform.GetWorldPosition());
		btVector3 rayEnd = rayStart + btVector3(0, - m_Height / 2.0f + 0.05f, 0);

		btDynamicsWorld::ClosestRayResultCallback rayCallback(rayStart, rayEnd);
		btDiscreteDynamicsWorld* physWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
		physWorld->rayTest(rayStart, rayEnd, rayCallback);
		m_bGrounded = rayCallback.hasHit();
	}

	void Player::UpdateIsPossessed()
	{
		m_bPossessed = false;

		// TODO: Implement more robust solution
		BaseCamera* cam = g_CameraManager->CurrentCamera();
		FirstPersonCamera* fpCam = dynamic_cast<FirstPersonCamera*>(cam);
		OverheadCamera* ohCam = dynamic_cast<OverheadCamera*>(cam);
		if (fpCam || ohCam)
		{
			m_bPossessed = true;
		}
	}

	real Player::GetDistAlongTrack() const
	{
		if (m_TrackRidingID == InvalidTrackID)
		{
			return -1.0f;
		}
		else
		{
			return m_DistAlongTrack;
		}
	}

	glm::vec3 Player::GetTrackPlacementReticlePosWS(real snapThreshold /* = -1.0f */, bool bSnapToHandles /* = false */) const
	{
		glm::vec3 offsetWS = m_TrackPlacementReticlePos;
		glm::mat4 rotMat = glm::mat4(m_Transform.GetWorldRotation());
		offsetWS = rotMat * glm::vec4(offsetWS, 1.0f);

		glm::vec3 point = m_Transform.GetWorldPosition() + offsetWS;

		if (snapThreshold != -1.0f)
		{
			glm::vec3 pointInRange;
			if (g_SceneManager->CurrentScene()->GetTrackManager()->GetPointInRange(point, bSnapToHandles, snapThreshold, &pointInRange))
			{
				point = pointInRange;
			}
		}

		return point;
	}

	void Player::AttachToTrack(TrackID trackID, real distAlongTrack)
	{
		TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
		BezierCurveList* track = trackManager->GetTrack(trackID);
		assert(track);

		if (m_TrackRidingID != InvalidTrackID)
		{
			PrintWarn("Player::AttachToTrack called when already attached! Detaching...\n");
			DetachFromTrack();
		}

		distAlongTrack = glm::clamp(distAlongTrack, 0.0f, 1.0f);

		m_TrackRidingID = trackID;
		m_DistAlongTrack = distAlongTrack;

		if (track->IsVectorFacingDownTrack(m_DistAlongTrack, m_Transform.GetForward()))
		{
			m_TrackState = TrackState::FACING_BACKWARD;
		}
		else
		{
			m_TrackState = TrackState::FACING_FORWARD;
		}

		AudioManager::PlaySource(m_SoundTrackAttachID);
	}

	void Player::DetachFromTrack()
	{
		if (m_TrackRidingID != InvalidTrackID)
		{
			m_TrackRidingID = InvalidTrackID;
			m_DistAlongTrack = -1.0f;
			AudioManager::PlaySource(m_SoundTrackDetachID);
		}
	}

	bool Player::IsFacingDownTrack() const
	{
		return (m_TrackState == TrackState::FACING_FORWARD);
	}

	void Player::BeginTurnTransition()
	{
		if (m_TrackState == TrackState::FACING_FORWARD)
		{
			m_TrackState = TrackState::FACING_BACKWARD;
		}
		else if (m_TrackState == TrackState::FACING_BACKWARD)
		{
			m_TrackState = TrackState::FACING_FORWARD;
		}
		else
		{
			PrintWarn("Unhandled track state when starting turn transition: %d/n", (i32)m_TrackState);
		}
	}

	void Player::AddToInventory(GameObject* obj)
	{
		m_Inventory.push_back(obj);
	}

} // namespace flex
