#include "stdafx.hpp"

#include "Player.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include <queue>

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/FirstPersonCamera.hpp"
#include "Cameras/OverheadCamera.hpp"
#include "Cameras/TerminalCamera.hpp"
#include "Cameras/VehicleCamera.hpp"
#include "Editor.hpp"
#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	Player::Player(i32 index, GameObjectID gameObjectID /* = InvalidGameObjectID */) :
		GameObject("Player " + std::to_string(index), SID("player"), gameObjectID),
		m_Index(index),
		m_TrackPlacementReticlePos(0.0f, -1.95f, 3.5f)
	{
	}

	void Player::Initialize()
	{
		m_SoundPlaceTrackNodeID = AudioManager::AddAudioSource(SFX_DIRECTORY "click-02.wav");
		m_SoundPlaceFinalTrackNodeID = AudioManager::AddAudioSource(SFX_DIRECTORY "jingle-single-01.wav");
		m_SoundTrackAttachID = AudioManager::AddAudioSource(SFX_DIRECTORY "crunch-13.wav");
		m_SoundTrackDetachID = AudioManager::AddAudioSource(SFX_DIRECTORY "schluck-02.wav");
		m_SoundTrackSwitchDirID = AudioManager::AddAudioSource(SFX_DIRECTORY "whistle-01.wav");
		//m_SoundTrackAttachID = AudioManager::AddAudioSource(SFX_DIRECTORY "schluck-07.wav");

		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "Player " + std::to_string(m_Index) + " material";
		matCreateInfo.shaderName = "pbr";
		matCreateInfo.constAlbedo = glm::vec3(0.89f, 0.93f, 0.98f);
		matCreateInfo.constMetallic = 0.0f;
		matCreateInfo.constRoughness = 0.98f;
		matCreateInfo.bSerializable = false;
		MaterialID matID = g_Renderer->InitializeMaterial(&matCreateInfo);

		RigidBody* rigidBody = new RigidBody();
		rigidBody->SetAngularDamping(2.0f);
		rigidBody->SetLinearDamping(0.1f);
		rigidBody->SetFriction(m_MoveFriction);

		btCapsuleShape* collisionShape = new btCapsuleShape(1.0f, 2.0f);

		m_Mesh = new Mesh(this);
		AddTag("Player" + std::to_string(m_Index));
		SetRigidBody(rigidBody);
		SetStatic(false);
		SetSerializable(false);
		SetCollisionShape(collisionShape);
		m_Mesh->LoadFromFile(MESH_DIRECTORY "capsule.glb", matID);

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
			mapTabletMatCreateInfo.bSerializable = false;
			MaterialID mapTabletMatID = g_Renderer->InitializeMaterial(&mapTabletMatCreateInfo);

			m_MapTabletHolder = new GameObject("Map tablet", SID("object"));
			m_MapTabletHolder->GetTransform()->SetLocalRotation(glm::quat(glm::vec3(0.0f, m_TabletOrbitAngle, 0.0f)));
			AddChild(m_MapTabletHolder);

			if (m_bTabletUp)
			{
				m_TabletOrbitAngle = m_TabletOrbitAngleUp;
			}
			else
			{
				m_TabletOrbitAngle = m_TabletOrbitAngleDown;
			}

			m_MapTablet = new GameObject("Map tablet mesh", SID("object"));
			Mesh* mapTabletMesh = m_MapTablet->SetMesh(new Mesh(m_MapTablet));
			mapTabletMesh->LoadFromFile(MESH_DIRECTORY "map_tablet.glb", mapTabletMatID);
			m_MapTabletHolder->AddChild(m_MapTablet);
			m_MapTablet->GetTransform()->SetLocalPosition(glm::vec3(-0.75f, -0.3f, 2.3f));
			m_MapTablet->GetTransform()->SetLocalRotation(glm::quat(glm::vec3(-glm::radians(80.0f), glm::radians(13.3f), -glm::radians(86.0f))));
		}

		m_CrosshairTextureID = g_Renderer->InitializeTextureFromFile(TEXTURE_DIRECTORY "cross-hair-01.png", false, false, false);

		GameObject::Initialize();
	}

	void Player::PostInitialize()
	{
		m_RigidBody->SetOrientationConstraint(btVector3(0.0f, 1.0f, 0.0f));
		m_RigidBody->GetRigidBodyInternal()->setSleepingThresholds(0.0f, 0.0f);

		GameObject::PostInitialize();
	}

	void Player::Destroy(bool bDetachFromParent /* = true */)
	{
		if (m_Controller)
		{
			m_Controller->Destroy();
			delete m_Controller;
		}

		AudioManager::DestroyAudioSource(m_SoundPlaceTrackNodeID);
		AudioManager::DestroyAudioSource(m_SoundPlaceFinalTrackNodeID);
		AudioManager::DestroyAudioSource(m_SoundTrackAttachID);
		AudioManager::DestroyAudioSource(m_SoundTrackDetachID);
		AudioManager::DestroyAudioSource(m_SoundTrackSwitchDirID);

		GameObject::Destroy(bDetachFromParent);
	}

	void Player::Update()
	{
		m_Controller->Update();

		if (m_TrackRidingID != InvalidTrackID)
		{
			TrackManager* trackManager = g_SceneManager->CurrentScene()->GetTrackManager();
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

		// Draw cross hair
		{
			SpriteQuadDrawInfo drawInfo = {};
			drawInfo.anchor = AnchorPoint::CENTER;
			drawInfo.bScreenSpace = true;
			drawInfo.bReadDepth = false;
			drawInfo.scale = glm::vec3(0.02f);
			drawInfo.textureID = m_CrosshairTextureID;
			g_Renderer->EnqueueSprite(drawInfo);
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

		if (m_ObjectInteractingWith != nullptr)
		{
			StringID interactingWithTypeID = m_ObjectInteractingWith->GetTypeID();
			switch (interactingWithTypeID)
			{
			case SID("terminal"):
			{
				if (g_EngineInstance->IsRenderingImGui())
				{
					Terminal* terminal = static_cast<Terminal*>(m_ObjectInteractingWith);
					terminal->DrawImGuiWindow();
				}
			} break;
			case SID("vehicle"):
			{
				Vehicle* vehicle = static_cast<Vehicle*>(m_ObjectInteractingWith);

				const glm::vec3 posOffset = glm::vec3(0.0f, 3.0f, 0.0f);

				m_Transform.SetWorldPosition(vehicle->GetTransform()->GetWorldPosition() + posOffset);
			} break;
			}
		}

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
		GameObject::DrawImGuiObjects();

		std::string treeNodeName = "Player " + IntToString(m_Index);
		if (ImGui::TreeNode(treeNodeName.c_str()))
		{
			if (m_ObjectInteractingWith != nullptr)
			{
				std::string name = m_ObjectInteractingWith->GetName();
				ImGui::Text("Object interacting with:");
				if (ImGui::Button(name.c_str()))
				{
					g_Editor->SetSelectedObject(m_ObjectInteractingWith->ID);
				}
			}

			ImGui::Text("Pitch: %.2f", GetPitch());
			glm::vec3 euler = glm::eulerAngles(GetTransform()->GetWorldRotation());
			ImGui::Text("World rot: %.2f, %.2f, %.2f", euler.x, euler.y, euler.z);

			bool bRiding = (m_TrackRidingID != InvalidTrackID);
			ImGui::Text("Riding track: %s", (bRiding ? "true" : "false"));
			if (bRiding)
			{
				ImGui::Indent();
				ImGui::Text("Dist along track: %.2f", GetDistAlongTrack());
				ImGui::Text("Track state: %s", TrackStateStrs[(i32)m_TrackState]);
				ImGui::Unindent();
			}

			ImGui::Text("Held item: %s", m_HeldItem != nullptr ? m_HeldItem->GetName().c_str() : "");

			ImGui::Text("Inventory:");
			ImGui::Indent();
			for (GameObject* gameObject : m_Inventory)
			{
				if (gameObject == m_HeldItem)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
				}

				ImGui::Text("%s", gameObject->GetName().c_str());

				if (gameObject == m_HeldItem)
				{
					ImGui::PopStyleColor();
				}
			}
			ImGui::Unindent();

			ImGui::TreePop();
		}
	}

	bool Player::AllowInteractionWith(GameObject* gameObject)
	{
		if (gameObject == nullptr)
		{
			return true;
		}

		StringID objTypeID = gameObject->GetTypeID();
		switch (objTypeID)
		{
		case SID("terminal"): return true;
		case SID("vehicle"): return true;
		}

		return false;
	}

	void Player::SetInteractingWith(GameObject* gameObject)
	{
		if (gameObject == nullptr)
		{
			StringID previousObjectInteractingWithID = m_ObjectInteractingWith->GetTypeID();
			switch (previousObjectInteractingWithID)
			{
			case SID("terminal"):
			{
				BaseCamera* cam = g_CameraManager->CurrentCamera();
				assert(cam->type == CameraType::TERMINAL);
				TerminalCamera* terminalCam = static_cast<TerminalCamera*>(cam);
				terminalCam->SetTerminal(nullptr);
			} break;
			case SID("vehicle"):
			{
				BaseCamera* cam = g_CameraManager->CurrentCamera();
				if (cam->type == CameraType::VEHICLE)
				{
					g_CameraManager->PopCamera();
				}

				SetVisible(true);
			} break;
			}

			GameObject::SetInteractingWith(nullptr);
			return;
		}

		StringID newObjectInteractingWithID = gameObject->GetTypeID();
		switch (newObjectInteractingWithID)
		{
		case SID("terminal"):
		{
			Terminal* terminal = static_cast<Terminal*>(gameObject);
			GameObject::SetInteractingWith(terminal);

			BaseCamera* cam = g_CameraManager->CurrentCamera();
			TerminalCamera* terminalCam = nullptr;
			if (cam->type == CameraType::TERMINAL)
			{
				terminalCam = static_cast<TerminalCamera*>(cam);
			}
			else
			{
				terminalCam = static_cast<TerminalCamera*>(g_CameraManager->GetCameraByName("terminal"));
				g_CameraManager->PushCamera(terminalCam, true, true);
			}
			terminalCam->SetTerminal(terminal);
		} break;
		case SID("wire"):
		{
			Wire* wire = static_cast<Wire*>(gameObject);

			m_HeldItem = wire;
		} break;
		case SID("socket"):
		{
			Socket* socket = static_cast<Socket*>(gameObject);

			if (m_HeldItem != nullptr && m_HeldItem->GetTypeID() == SID("wire"))
			{
				Wire* wire = (Wire*)m_HeldItem;
				if (wire->socket0ID.IsValid() && wire->socket1ID.IsValid())
				{
					wire->SetInteractingWith(nullptr);
					m_HeldItem = nullptr;
				}
			}
			else
			{
				if (socket->connectedWire != nullptr)
				{
					m_HeldItem = socket->connectedWire;
				}
			}
		} break;
		case SID("vehicle"):
		{
			Vehicle* vehicle = static_cast<Vehicle*>(gameObject);
			GameObject::SetInteractingWith(vehicle);

			BaseCamera* cam = g_CameraManager->CurrentCamera();
			VehicleCamera* vehicleCamera = nullptr;
			if (cam->type == CameraType::VEHICLE)
			{
				vehicleCamera = static_cast<VehicleCamera*>(cam);
			}
			else
			{
				vehicleCamera = static_cast<VehicleCamera*>(g_CameraManager->GetCameraByName("vehicle"));
				g_CameraManager->PushCamera(vehicleCamera, false, true);
			}

			SetVisible(false);
		} break;
		default:
		{
			GameObject::SetInteractingWith(gameObject);
		} break;
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
		btVector3 rayEnd = rayStart + btVector3(0, -m_Height / 2.0f + 0.05f, 0);

		btDynamicsWorld::ClosestRayResultCallback rayCallback(rayStart, rayEnd);
		btDiscreteDynamicsWorld* physWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
		physWorld->rayTest(rayStart, rayEnd, rayCallback);
		m_bGrounded = rayCallback.hasHit();
	}

	void Player::UpdateIsPossessed()
	{
		m_bPossessed = false;

		BaseCamera* cam = g_CameraManager->CurrentCamera();
		m_bPossessed = cam->bPossessPlayer;
		m_Controller->UpdateMode();
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

		distAlongTrack = Saturate(distAlongTrack);

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

	bool Player::IsRidingTrack()
	{
		return m_TrackRidingID != InvalidTrackID;
	}
} // namespace flex
