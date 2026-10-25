#include "stdafx.hpp"

#include "Player.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btManifoldResult.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/norm.hpp> // For distance2
IGNORE_WARNINGS_POP

#include <queue>

#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/TerminalCamera.hpp"
#include "Cameras/VehicleCamera.hpp"
#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "Inventory.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "PlayerController.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/Objects/Industry.hpp"
#include "Scene/Objects/Props.hpp"
#include "Scene/Objects/Terminal.hpp"
#include "Scene/Objects/TerrainGenerator.hpp"
#include "Scene/Objects/Vehicle.hpp"
#include "Scene/Objects/Wires.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/TrackManager.hpp"
#include "Traversal.hpp"

namespace flex
{
	const glm::vec3 Player::HEADLAMP_MOUNT_POS = glm::vec3(0.0f, 0.8f, 0.2f);

	// Removes friction from contacts against steep surfaces (walls) so the player slides along them rather than sticking
	static bool PlayerContactAddedCallback(btManifoldPoint& cp, const btCollisionObjectWrapper* colObj0Wrap, int partId0, int index0,
		const btCollisionObjectWrapper* colObj1Wrap, int partId1, int index1)
	{
		FLEX_UNUSED(colObj0Wrap);
		FLEX_UNUSED(partId0);
		FLEX_UNUSED(index0);
		FLEX_UNUSED(colObj1Wrap);
		FLEX_UNUSED(partId1);
		FLEX_UNUSED(index1);

		// Surfaces steeper than ~45 degrees are treated as walls
		const btScalar maxWalkableNormalY = 0.7f;
		if (btFabs(cp.m_normalWorldOnB.getY()) < maxWalkableNormalY)
		{
			cp.m_combinedFriction = 0.0f;
		}

		return true;
	}

	Player::Player(i32 index, GameObjectID gameObjectID) :
		GameObject("Player " + std::to_string(index), PlayerSID, gameObjectID, InvalidPrefabIDPair, false),
		m_Index(index)
	{
		m_TrackBuildingContext = {};
	}

	PropertyCollection* Player::BuildTypeUniquePropertyCollection()
	{
		PropertyCollection* collection = GameObject::BuildPropertyCollection();

		return collection;
	}

	void Player::Initialize()
	{
		m_SoundPlaceTrackNodeID = g_ResourceManager->GetOrLoadAudioSourceID(SID("click-02.wav"), true);
		m_SoundPlaceFinalTrackNodeID = g_ResourceManager->GetOrLoadAudioSourceID(SID("jingle-single-01.wav"), true);
		m_SoundTrackAttachID = g_ResourceManager->GetOrLoadAudioSourceID(SID("crunch-13.wav"), true);
		m_SoundTrackDetachID = g_ResourceManager->GetOrLoadAudioSourceID(SID("schluck-02.wav"), true);
		m_SoundTrackSwitchDirID = g_ResourceManager->GetOrLoadAudioSourceID(SID("whistle-01.wav"), true);
		//m_SoundTrackAttachID = g_ResourceManager->GetOrLoadAudioSourceID(SID("schluck-07.wav"), true);

		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "Player " + std::to_string(m_Index) + " material";
		matCreateInfo.shaderName = "pbr";
		matCreateInfo.constAlbedo = glm::vec4(0.89f, 0.93f, 0.98f, 1.0f);
		matCreateInfo.constMetallic = 0.0f;
		matCreateInfo.constRoughness = 0.98f;
		matCreateInfo.bSerializable = false;
		MaterialID matID = g_Renderer->InitializeMaterial(&matCreateInfo);

		RigidBody* rigidBody = new RigidBody((u32)CollisionType::DEFAULT, (u32)CollisionType::STATIC | (u32)CollisionType::DEFAULT);
		rigidBody->SetAngularDamping(2.0f);
		rigidBody->SetLinearDamping(0.1f);
		rigidBody->SetFriction(m_MoveFriction);

		btCapsuleShape* collisionShape = new btCapsuleShape(1.0f, 2.0f);

		GameObject* playerMeshObject = new GameObject("Player mesh", BaseObjectSID);
		Mesh* playerMesh = playerMeshObject->SetMesh(new Mesh(playerMeshObject));
		AddTag("Player" + std::to_string(m_Index));
		SetRigidBody(rigidBody);
		SetStatic(false);
		SetSerializable(false);
		SetCollisionShape(collisionShape);
		playerMesh->LoadFromFile(MESH_DIRECTORY "capsule.glb", matID);
		playerMeshObject->GetTransform()->SetLocalRotation(glm::quat(glm::vec3(-PI_DIV_TWO, 0.0f, 0.0f)));
		AddChild(playerMeshObject);

		m_Controller = new PlayerController();
		m_Controller->Initialize(this);

		m_Traversal = new Traversal();
		m_Traversal->Initialize(this);

		TextureLoadInfo loadInfo = {};
		loadInfo.relativeFilePath = TEXTURE_DIRECTORY "cross-hair-01.png";
		loadInfo.sampler = g_Renderer->GetSamplerLinearClampToEdge();
		m_CrosshairTextureID = g_ResourceManager->QueueTextureLoad(loadInfo);

		ParseInventoryFile();

		GameObject::Initialize();
	}

	void Player::PostInitialize()
	{
		// Orientation is driven entirely by the controller, don't let contacts spin the player
		m_RigidBody->SetOrientationConstraint(btVector3(0.0f, 0.0f, 0.0f));
		m_RigidBody->GetRigidBodyInternal()->setSleepingThresholds(0.0f, 0.0f);

		btRigidBody* rbInternal = m_RigidBody->GetRigidBodyInternal();
		rbInternal->setCollisionFlags(rbInternal->getCollisionFlags() | btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK);
		gContactAddedCallback = PlayerContactAddedCallback;

		// Rotation is set every frame by the controller, so only position needs smoothing between fixed steps
		m_RigidBody->SetInterpolation(true, false);

		GameObject::PostInitialize();
	}

	void Player::Destroy(bool bDetachFromParent /* = true */)
	{
		if (m_Traversal != nullptr)
		{
			m_Traversal->Destroy();
			delete m_Traversal;
			m_Traversal = nullptr;
		}

		if (m_Controller != nullptr)
		{
			m_Controller->Destroy();
			delete m_Controller;
		}

		g_ResourceManager->DestroyAudioSource(m_SoundPlaceTrackNodeID);
		g_ResourceManager->DestroyAudioSource(m_SoundPlaceFinalTrackNodeID);
		g_ResourceManager->DestroyAudioSource(m_SoundTrackAttachID);
		g_ResourceManager->DestroyAudioSource(m_SoundTrackDetachID);
		g_ResourceManager->DestroyAudioSource(m_SoundTrackSwitchDirID);

		GameObject::Destroy(bDetachFromParent);
	}

	void Player::Update()
	{
		UpdateTerrainSafety();

		m_Controller->Update();
		m_Traversal->Update();

		UpdateTerrainTool();

		if (m_TrackRidingID != InvalidTrackID)
		{
			TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
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

		if (m_TerminalInteractingWithID.IsValid() && g_EngineInstance->IsRenderingImGui())
		{
			Terminal* terminal = (Terminal*)m_TerminalInteractingWithID.Get();
			terminal->DrawImGuiWindow();
		}

		if (m_RidingVehicleID.IsValid())
		{
			Vehicle* vehicle = (Vehicle*)m_RidingVehicleID.Get();

			const glm::vec3 posOffset = glm::vec3(0.0f, 3.0f, 0.0f);

			m_Transform.SetWorldPosition(vehicle->GetTransform()->GetWorldPosition() + posOffset);
		}

		// Terrain can extend far below the scene's minimum height (UpdateTerrainSafety handles falling out of it)
		BaseScene* scene = g_SceneManager->CurrentScene();
		if (TerrainGenerator::GetActive() == nullptr && m_Transform.GetWorldPosition().y < scene->GetPlayerMinHeight())
		{
			Reset();
		}

		std::vector<DroppedItem*> nearbyItems;
		if (scene->GetDroppedItemsInRadius(m_Transform.GetWorldPosition(), m_ItemPickupRadius, nearbyItems))
		{
			for (DroppedItem* item : nearbyItems)
			{
				if (item->CanBePickedUp())
				{
					AddToInventory(item);
				}
			}
		}

		if (m_HeldItemLeftHand.IsValid())
		{
			GameObject::UpdateHeldItem(m_HeldItemLeftHand);
		}

		if (m_HeldItemRightHand.IsValid())
		{
			GameObject::UpdateHeldItem(m_HeldItemRightHand);
		}

		GameObjectStack& stack = m_QuickAccessInventory[m_SelectedQuickAccessItemSlot];
		PrefabID desiredActiveItemPrefabID = InvalidPrefabID;
		if (stack.count > 1 || (stack.count == 1 && !m_bPreviewPlaceItemFromInventory))
		{
			desiredActiveItemPrefabID = stack.prefabID;
		}

		ActiveItem* activeItem = m_ActiveItemIndex != -1 ? &m_ActiveItemCache[m_ActiveItemIndex] : nullptr;
		if (activeItem == nullptr || activeItem->m_SourcePrefabID != desiredActiveItemPrefabID)
		{
			// Proxies are cached & hidden rather than destroyed since creating a render object
			// stalls the GPU to rebuild static vertex buffers, causing a hitch on every item switch
			if (activeItem != nullptr)
			{
				activeItem->m_ItemProxyObject->SetVisible(false);
			}
			activeItem = nullptr;
			m_ActiveItemIndex = -1;

			if (desiredActiveItemPrefabID.IsValid())
			{
				for (i32 i = 0; i < (i32)m_ActiveItemCache.size(); ++i)
				{
					if (m_ActiveItemCache[i].m_SourcePrefabID == desiredActiveItemPrefabID)
					{
						m_ActiveItemIndex = i;
						break;
					}
				}

				if (m_ActiveItemIndex == -1)
				{
					m_ActiveItemCache.emplace_back();
					m_ActiveItemCache.back().Create(desiredActiveItemPrefabID);
					m_ActiveItemIndex = (i32)m_ActiveItemCache.size() - 1;
				}

				activeItem = &m_ActiveItemCache[m_ActiveItemIndex];
				activeItem->m_ItemProxyObject->SetVisible(true);
			}
		}

		if (activeItem != nullptr)
		{
			GameObject::UpdateActiveItem(*activeItem);
		}

		if (m_ItemPickingTimer != -1.0f)
		{
			m_ItemPickingTimer -= g_DeltaTime;

			if (m_ItemPickingTimer <= 0.0f)
			{
				GameObjectStack::UserData itemUserData = {};
				PrefabID itemID = m_ItemPickingUp->Itemize(itemUserData);
				AddToInventory(itemID, 1, itemUserData);

				SetItemPickingUp(nullptr);
			}
			else
			{
				GameObject* pickedItem = GetObjectPointedAt();
				if (pickedItem == nullptr || pickedItem != m_ItemPickingUp)
				{
					SetItemPickingUp(nullptr);
				}
				else
				{
					real startAngle = PI_DIV_TWO - (1.0f - m_ItemPickingTimer / m_ItemPickingDuration) * TWO_PI;
					real endAngle = PI_DIV_TWO;
					g_Renderer->GetUIMesh()->DrawArc(VEC2_ZERO, startAngle, endAngle, 0.05f, 0.025f, 32, VEC4_ONE);
				}
			}
		}

		GameObject::Update();
	}

	void Player::FixedUpdate()
	{
		GameObject::FixedUpdate();

		m_Controller->FixedUpdate();
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

	void Player::Reset()
	{
		// Spawn points aren't meaningful in streamed terrain, so respawn on the surface nearby instead
		m_bTerrainPlacementPending = true;
		m_Traversal->Reset();

		glm::vec3 spawnPoint = g_SceneManager->CurrentScene()->GetPlayerSpawnPoint();
		m_Transform.SetWorldPosition(spawnPoint);
		m_Transform.SetWorldRotation(QUAT_IDENTITY);
		btRigidBody* rigidBodyInternal = m_RigidBody->GetRigidBodyInternal();
		rigidBodyInternal->clearForces();
		rigidBodyInternal->clearGravity();
		rigidBodyInternal->setLinearVelocity(btVector3(0, 0, 0));
		rigidBodyInternal->setAngularVelocity(btVector3(0, 0, 0));

		m_Pitch = 0.0f;
	}

	glm::quat Player::GetLookYawRotation() const
	{
		// While riding, the body faces down the track and the head can look around independently
		glm::quat rotWS = m_Transform.GetWorldRotation();
		if (m_RidingLookYaw != 0.0f)
		{
			rotWS = glm::rotate(rotWS, m_RidingLookYaw, VEC3_UP);
		}
		return rotWS;
	}

	glm::vec3 Player::GetLookDirection() const
	{
		glm::quat yawRot = GetLookYawRotation();
		glm::vec3 lookDir = yawRot * VEC3_FORWARD;
		lookDir = glm::rotate(lookDir, m_Pitch, yawRot * VEC3_RIGHT);

		return glm::normalize(lookDir);
	}

	glm::quat Player::GetLookRotation() const
	{
		glm::quat yawRot = GetLookYawRotation();
		glm::quat rotWS = glm::rotate(QUAT_IDENTITY, m_Pitch, yawRot * VEC3_RIGHT);
		rotWS *= yawRot;
		return rotWS;
	}

	glm::vec3 Player::GetHeldItemPosWS(Hand hand) const
	{
		glm::vec3 right = m_Transform.GetRight();

		glm::vec3 offset = m_Transform.GetWorldPosition() +
			GetLookDirection() * 5.0f +
			m_Transform.GetUp() * -0.75f +
			((hand == Hand::LEFT) ? -right : right);
		return offset;
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

	template<typename T>
	void DrawInventoryImGui(const char* inventoryName, i32 highlightedIndex, const T& inventory)
	{
		if (ImGui::TreeNode(inventoryName))
		{
			for (i32 i = 0; i < (i32)inventory.size(); ++i)
			{
				const bool bHeld = (i == highlightedIndex);
				if (bHeld)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
				}

				if (inventory[i].count != 0)
				{
					GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(inventory[i].prefabID);
					if (prefabTemplate != nullptr)
					{
						std::string prefabTemplateName = prefabTemplate->GetName();
						ImGui::Text("%s (%i), User data: %.3f", prefabTemplateName.c_str(), inventory[i].count, inventory[i].userData.floatVal);
					}
					else
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
						ImGui::Text("INVALID (%i)", inventory[i].count);
						ImGui::PopStyleColor();
					}
				}

				if (bHeld)
				{
					ImGui::PopStyleColor();
				}
			}
			ImGui::TreePop();
		}
	}

	void Player::DrawImGuiObjects(bool bDrawingEditorObjects)
	{
		GameObject::DrawImGuiObjects(bDrawingEditorObjects);

		std::string treeNodeName = "Player " + IntToString(m_Index);
		if (ImGui::TreeNode(treeNodeName.c_str()))
		{
			m_Traversal->DrawImGui();

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

			ImGui::Text("Selected slot: %i", m_SelectedQuickAccessItemSlot);

			{
				char idBuff[33];

				if (m_HeldItemLeftHand.IsValid())
				{
					m_HeldItemLeftHand.ToString(idBuff);
					ImGui::TextWrapped("Held item (L):\n  %s (%s)", idBuff, m_HeldItemLeftHand.Get()->GetName().c_str());
				}
				else
				{
					ImGui::TextWrapped("Held item (L): Empty");
				}

				if (m_HeldItemRightHand.IsValid())
				{
					m_HeldItemRightHand.ToString(idBuff);
					ImGui::TextWrapped("Held item (R):\n  %s (%s)", idBuff, m_HeldItemRightHand.Get()->GetName().c_str());
				}
				else
				{
					ImGui::TextWrapped("Held item (R): Empty");
				}
			}

			DrawInventoryImGui("Inventory", -1, m_Inventory);
			DrawInventoryImGui("Quick access inventory", m_SelectedQuickAccessItemSlot, m_QuickAccessInventory);
			DrawInventoryImGui("Wearables inventory", -1, m_WearablesInventory);

			m_Controller->DrawImGuiObjects();

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
		// Reaches a little past the bottom of the capsule so the ground is still found on slopes (where the capsule's
		// lowest point isn't directly below its center)
		btVector3 rayStart = ToBtVec3(m_Transform.GetWorldPosition());
		btVector3 rayEnd = rayStart + btVector3(0, -(m_Height / 2.0f + 0.5f), 0);

		struct IgnoreSelfRayCallback final : public btDynamicsWorld::ClosestRayResultCallback
		{
			IgnoreSelfRayCallback(const btVector3& from, const btVector3& to, const btCollisionObject* self) :
				ClosestRayResultCallback(from, to),
				self(self)
			{
			}

			virtual bool needsCollision(btBroadphaseProxy* proxy) const override
			{
				return proxy->m_clientObject != self && ClosestRayResultCallback::needsCollision(proxy);
			}

			const btCollisionObject* self;
		};

		IgnoreSelfRayCallback rayCallback(rayStart, rayEnd, m_RigidBody->GetRigidBodyInternal());
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

	void Player::SetInteractingWithTerminal(Terminal* terminal)
	{
		if (terminal != nullptr)
		{
			CHECK(!m_TerminalInteractingWithID.IsValid());

			BaseCamera* currCam = g_CameraManager->CurrentCamera();
			TerminalCamera* terminalCam = nullptr;
			if (currCam->type == CameraType::TERMINAL)
			{
				terminalCam = static_cast<TerminalCamera*>(currCam);
				terminalCam->SetTerminal(terminal);
			}
			else
			{
				terminalCam = g_CameraManager->GetOrCreateCameraByName<TerminalCamera>("terminal");
				g_CameraManager->AlignCameras(currCam, terminalCam);
				terminalCam->SetTerminal(terminal);
				g_CameraManager->PushCamera(terminalCam, true, true);
			}

			m_TerminalInteractingWithID = terminal->ID;
			terminal->SetBeingInteractedWith(this);
		}
		else
		{
			Terminal* terminalInteractingWith = (Terminal*)m_TerminalInteractingWithID.Get();
			if (terminalInteractingWith != nullptr)
			{
				BaseCamera* cam = g_CameraManager->CurrentCamera();
				CHECK_EQ((u32)cam->type, (u32)CameraType::TERMINAL);
				TerminalCamera* terminalCam = static_cast<TerminalCamera*>(cam);
				terminalCam->SetTerminal(nullptr);

				terminalInteractingWith->SetBeingInteractedWith(nullptr);
				m_TerminalInteractingWithID = InvalidGameObjectID;
			}
		}
	}

	void Player::SetRidingVehicle(Vehicle* vehicle)
	{
		if (vehicle != nullptr)
		{
			CHECK(!m_RidingVehicleID.IsValid());

			vehicle->OnPlayerEnter();

			m_RidingVehicleID = vehicle->ID;

			BaseCamera* cam = g_CameraManager->CurrentCamera();
			VehicleCamera* vehicleCamera = nullptr;
			// Switch to vehicle cam
			if (cam->type != CameraType::VEHICLE)
			{
				vehicleCamera = g_CameraManager->GetOrCreateCameraByName<VehicleCamera>("vehicle");
				g_CameraManager->PushCamera(vehicleCamera, false, true);
			}

			SetVisible(false);
		}
		else
		{
			Vehicle* ridingVehicle = (Vehicle*)m_RidingVehicleID.Get();
			ridingVehicle->OnPlayerExit();

			m_RidingVehicleID = InvalidGameObjectID;

			BaseCamera* cam = g_CameraManager->CurrentCamera();
			if (cam->type == CameraType::VEHICLE)
			{
				// Cycling cameras while riding clears the stack, leaving nothing beneath the vehicle cam to return to
				if (g_CameraManager->GetCameraStackSize() > 1)
				{
					g_CameraManager->PopCamera();
				}
				else
				{
					g_CameraManager->SetCameraByName("first-person", false);
				}
			}

			SetVisible(true);
		}
	}

	bool Player::PickupWithFreeHand(GameObject* object)
	{
		if (!m_HeldItemLeftHand.IsValid())
		{
			m_HeldItemLeftHand = object->ID;
			return true;
		}
		if (!m_HeldItemRightHand.IsValid())
		{
			m_HeldItemRightHand = object->ID;
			return true;
		}
		return false;
	}

	bool Player::IsHolding(GameObject* object)
	{
		return m_HeldItemLeftHand == object->ID || m_HeldItemRightHand == object->ID;
	}

	void Player::DropIfHolding(GameObject* object)
	{
		if (object != nullptr)
		{
			if (m_HeldItemLeftHand == object->ID)
			{
				m_HeldItemLeftHand = InvalidGameObjectID;
			}
			if (m_HeldItemRightHand == object->ID)
			{
				m_HeldItemRightHand = InvalidGameObjectID;
			}
		}
	}

	bool Player::HasFreeHand() const
	{
		return !m_HeldItemLeftHand.IsValid() || !m_HeldItemRightHand.IsValid();
	}

	void Player::SetHeldItem(Hand hand, GameObjectID gameObjectID)
	{
		if (hand == Hand::LEFT)
		{
			m_HeldItemLeftHand = gameObjectID;
		}
		else
		{
			m_HeldItemRightHand = gameObjectID;
		}
	}

	void Player::SpawnWire()
	{
		if (!m_HeldItemLeftHand.IsValid() && !m_HeldItemRightHand.IsValid())
		{
			BaseScene* currentScene = g_SceneManager->CurrentScene();
			Wire* wire = (Wire*)GameObject::CreateObjectOfType(WireSID, currentScene->GetUniqueObjectName("wire_", 3));

			Transform* wireTransform = wire->GetTransform();

			glm::vec3 targetPos = m_Transform.GetWorldPosition() +
				GetLookDirection() * 5.0f +
				m_Transform.GetUp() * -0.75f;

			wireTransform->SetWorldPosition(targetPos, false);

			currentScene->AddRootObject(wire);

			CHECK(!GetHeldItem(Hand::LEFT).IsValid());
			CHECK(!GetHeldItem(Hand::RIGHT).IsValid());

			SetHeldItem(Hand::LEFT, wire->plug0ID);
			SetHeldItem(Hand::RIGHT, wire->plug1ID);
		}
	}

	bool Player::AbleToInteract() const
	{
		return m_bPossessed  &&
			!m_RidingVehicleID.IsValid() &&
			!m_TerminalInteractingWithID.IsValid();
	}

	void Player::ResetItemPickingTimer()
	{
		m_ItemPickingTimer = -1.0f;
	}

	GameObject* Player::GetObjectPointedAt() const
	{
		PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();

		btVector3 rayStart, rayEnd;
		FlexEngine::GenerateRayAtScreenCenter(rayStart, rayEnd, m_ItemPickupMaxDist);

		const btRigidBody* pickedBody = physicsWorld->PickFirstBody(rayStart, rayEnd);

		if (pickedBody != nullptr)
		{
			GameObject* gameObject = static_cast<GameObject*>(pickedBody->getUserPointer());
			real dist = glm::distance(gameObject->GetTransform()->GetWorldPosition(), m_Transform.GetWorldPosition());
			if (dist < m_ItemPickupMaxDist && gameObject->IsItemizable())
			{
				// Minerals can only be mined with a pickaxe
				if (gameObject->GetTypeID() == MineralDepositSID && !IsWieldingPickAxe())
				{
					return nullptr;
				}

				return gameObject;
			}
		}

		return nullptr;
	}

	void Player::DrawTraversalHUD() const
	{
		m_Traversal->DrawHUD();
	}

	void Player::OnRenderPoseUpdated()
	{
		m_Traversal->UpdateVisuals();
	}

	void Player::SetItemPickingUp(GameObject* pickedItem)
	{
		if (pickedItem == nullptr)
		{
			m_ItemPickingUp = nullptr;
			ResetItemPickingTimer();
			return;
		}

		if (m_ItemPickingUp != nullptr)
		{
			PrintError("Attempted to pick up item while m_ItemPickingUp was non-null, ignoring additional requests\n");
			return;
		}

		m_ItemPickingUp = pickedItem;
		m_ItemPickingTimer = m_ItemPickingDuration;
	}

} // namespace flex
