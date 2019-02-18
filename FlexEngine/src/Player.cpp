#include "stdafx.hpp"

#include "Player.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <LinearMath/btIDebugDraw.h>

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
#include "Graphics/Renderer.hpp"
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
		rigidBody->SetAngularDamping(2.0f);
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

			m_MapTabletHolder = new GameObject("Map tablet", GameObjectType::_NONE);
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

			m_MapTablet = new GameObject("Map tablet mesh", GameObjectType::_NONE);
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

		// Draw crosshair
		{
			SpriteQuadDrawInfo drawInfo = {};
			drawInfo.anchor = AnchorPoint::CENTER;
			drawInfo.bScreenSpace = true;
			drawInfo.bWriteDepth = false;
			drawInfo.bReadDepth = false;
			drawInfo.scale = glm::vec3(0.02f);
			drawInfo.textureHandleID = g_Renderer->GetTextureHandle(m_CrosshairTextureID);
			g_Renderer->DrawSprite(drawInfo);
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
		GameObject::DrawImGuiObjects();

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
				ImGui::Text("Track state: %s", TrackStateStrs[(i32)m_TrackState]);
				ImGui::Unindent();
			}

			ImGui::Text("Inventory:");
			ImGui::Indent();
			for (GameObject* gameObject : m_Inventory)
			{
				ImGui::Text("%s", gameObject->GetName().c_str());
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

		Terminal* terminal = dynamic_cast<Terminal*>(gameObject);
		if (terminal != nullptr)
		{
			return true;
		}

		return false;
	}

	void Player::SetInteractingWith(GameObject* gameObject)
	{
		if (gameObject == nullptr)
		{
			Terminal* terminalWasInteractingWith = dynamic_cast<Terminal*>(m_ObjectInteractingWith);
			if (terminalWasInteractingWith != nullptr)
			{
				TerminalCamera* terminalCam = (TerminalCamera*)g_CameraManager->CurrentCamera();
				terminalCam->SetTerminal(nullptr);
				GameObject::SetInteractingWith(gameObject);
			}

			return;
		}

		Terminal* terminal = dynamic_cast<Terminal*>(gameObject);
		if (terminal != nullptr)
		{
			m_ObjectInteractingWith = gameObject;
			m_bBeingInteractedWith = true;

			TerminalCamera* terminalCam = dynamic_cast<TerminalCamera*>(g_CameraManager->CurrentCamera());
			bool bNewCam = false;
			if (terminalCam == nullptr)
			{
				terminalCam = (TerminalCamera*)g_CameraManager->GetCameraByName("terminal");
				bNewCam = true;
			}
			terminalCam->SetTerminal(terminal);
			if (bNewCam)
			{
				g_CameraManager->PushCamera(terminalCam, true);
			}
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

		// TODO: Implement more robust solution
		BaseCamera* cam = g_CameraManager->CurrentCamera();
		FirstPersonCamera* fpCam = dynamic_cast<FirstPersonCamera*>(cam);
		OverheadCamera* ohCam = dynamic_cast<OverheadCamera*>(cam);
		if (fpCam != nullptr || ohCam != nullptr)
		{
			m_bPossessed = true;
		}

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

	bool Player::IsRidingTrack()
	{
		return m_TrackRidingID != InvalidTrackID;
	}

	void* Player::operator new(size_t i)
	{
		return _mm_malloc(i, 16);
	}

	void Player::operator delete(void* p)
	{
		_mm_free(p);
	}
} // namespace flex
