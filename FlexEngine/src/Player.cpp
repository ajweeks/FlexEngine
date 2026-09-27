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

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/FirstPersonCamera.hpp"
#include "Cameras/OverheadCamera.hpp"
#include "Cameras/TerminalCamera.hpp"
#include "Cameras/VehicleCamera.hpp"
#include "Editor.hpp"
#include "FlexEngine.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "Inventory.hpp"
#include "JSONParser.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "PlayerController.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"
#include "StringBuilder.hpp"
#include "Systems/TrackManager.hpp"
#include "Window/Window.hpp"

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
		m_Controller->Update();

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

		BaseScene* scene = g_SceneManager->CurrentScene();
		if (m_Transform.GetWorldPosition().y < scene->GetPlayerMinHeight())
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

	void Player::AttachToTrack(TrackID trackID, real distAlongTrack)
	{
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
		BezierCurveList* track = trackManager->GetTrack(trackID);
		CHECK_NE(track, nullptr);

		if (m_TrackRidingID != InvalidTrackID)
		{
			PrintWarn("Player::AttachToTrack called when already attached! Detaching...\n");
			DetachFromTrack();
		}

		distAlongTrack = Saturate(distAlongTrack);

		m_TrackRidingID = trackID;
		m_DistAlongTrack = distAlongTrack;

		// Keep facing the way the player was looking. IsVectorFacingDownTrack is true when the vector points towards
		// decreasing t, which is the way FACING_FORWARD faces
		if (track->IsVectorFacingDownTrack(m_DistAlongTrack, GetLookDirection()))
		{
			m_TrackState = TrackState::FACING_FORWARD;
		}
		else
		{
			m_TrackState = TrackState::FACING_BACKWARD;
		}

		AudioManager::PlaySource(m_SoundTrackAttachID);
	}

	void Player::DetachFromTrack()
	{
		if (m_TrackRidingID != InvalidTrackID)
		{
			// Hop off to the side the player is looking towards so it's clear they've left the track
			TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
			glm::vec3 trackDir = trackManager->GetTrack(m_TrackRidingID)->GetCurveDirectionAt(m_DistAlongTrack);
			trackDir.y = 0.0f;
			if (glm::length2(trackDir) > 0.0001f)
			{
				const glm::vec3 trackRight = glm::normalize(glm::cross(VEC3_UP, glm::normalize(trackDir)));
				const real side = glm::dot(GetLookDirection(), trackRight) >= 0.0f ? 1.0f : -1.0f;
				const real hopDist = 1.75f;
				m_Transform.SetWorldPosition(m_Transform.GetWorldPosition() + trackRight * side * hopDist + VEC3_UP * 0.25f);
			}

			m_TrackRidingID = InvalidTrackID;
			m_DistAlongTrack = -1.0f;
			m_Controller->ResetForkSteer();

			// Turn the body to face where the player was looking so the view doesn't jump
			if (m_RidingLookYaw != 0.0f)
			{
				m_Transform.SetWorldRotation(GetLookYawRotation());
				m_RidingLookYaw = 0.0f;
			}
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

	void Player::DropSelectedItem()
	{
		DropItemStack(GetGameObjectStackIDForQuickAccessInventory(m_SelectedQuickAccessItemSlot), false);
	}

	bool Player::HasFullSelectedInventorySlot()
	{
		return m_QuickAccessInventory[m_SelectedQuickAccessItemSlot].count > 0;
	}

	i32 Player::GetNextFreeInventorySlot()
	{
		for (i32 i = 0; i < (i32)m_Inventory.size(); ++i)
		{
			if (m_Inventory[i].count == 0)
			{
				return i;
			}
		}

		return -1;
	}

	i32 Player::GetNextFreeQuickAccessInventorySlot()
	{
		for (i32 i = 0; i < (i32)m_QuickAccessInventory.size(); ++i)
		{
			if (m_QuickAccessInventory[i].count == 0)
			{
				return i;
			}
		}

		return -1;
	}

	i32 Player::GetNextFreeMinerInventorySlot()
	{
		if (m_NearbyInteractableID.IsValid() && m_NearbyInteractableID.Get()->GetTypeID() == MinerSID)
		{
			Miner* miner = (Miner*)m_NearbyInteractableID.Get();
			return miner->GetNextFreeInventorySlot();
		}
		else
		{
			PrintWarn("Attempted to get free inventory slot from miner inventory when not being interacted with\n");
			return -1;
		}
	}

	bool Player::IsRidingTrack()
	{
		return m_TrackRidingID != InvalidTrackID;
	}

	GameObjectStack* Player::GetGameObjectStackFromInventory(GameObjectStackID stackID, InventoryType& outInventoryType)
	{
		if ((i32)stackID < INVENTORY_MAX)
		{
			outInventoryType = InventoryType::PLAYER_INVENTORY;
			return &m_Inventory[(i32)stackID - INVENTORY_MIN];
		}
		if ((i32)stackID < INVENTORY_QUICK_ACCESS_MAX)
		{
			outInventoryType = InventoryType::QUICK_ACCESS;
			return &m_QuickAccessInventory[(i32)stackID - INVENTORY_QUICK_ACCESS_MIN];
		}
		if ((i32)stackID < INVENTORY_WEARABLES_MAX)
		{
			outInventoryType = InventoryType::WEARABLES;
			return &m_WearablesInventory[(i32)stackID - INVENTORY_WEARABLES_MIN];
		}
		if ((i32)stackID < INVENTORY_MINER_MAX)
		{
			if (m_NearbyInteractableID.IsValid() && m_NearbyInteractableID.Get()->GetTypeID() == MinerSID)
			{
				Miner* miner = (Miner*)m_NearbyInteractableID.Get();
				outInventoryType = InventoryType::MINER_INVENTORY;
				return miner->GetStackFromInventory((i32)stackID - INVENTORY_MINER_MIN);
			}
			else
			{
				PrintWarn("Attempted to get game object from miner inventory when not being interacted with\n");
				outInventoryType = InventoryType::NONE;
				return nullptr;
			}
		}

		outInventoryType = InventoryType::NONE;
		PrintWarn("Attempted to get item from inventory with invalid stackID: %d!\n", (i32)stackID);
		return nullptr;
	}

	bool Player::MoveItemStack(GameObjectStackID fromID, GameObjectStackID toID)
	{
		InventoryType fromInventoryType, toInventoryType;
		GameObjectStack* fromStack = GetGameObjectStackFromInventory(fromID, fromInventoryType);
		GameObjectStack* toStack = GetGameObjectStackFromInventory(toID, toInventoryType);

		if (fromStack != nullptr && toStack != nullptr && fromStack != toStack)
		{
			bool bFromWearables = fromInventoryType == InventoryType::WEARABLES;
			bool bToWearables = toInventoryType == InventoryType::WEARABLES;

			if (bToWearables)
			{
				GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(fromStack->prefabID);
				if (prefabTemplate->IsWearable())
				{
					if (toStack->count == 0 && fromStack->count == 1)
					{
						toStack->prefabID = fromStack->prefabID;
						toStack->count = fromStack->count;
						toStack->userData = fromStack->userData;
						fromStack->Clear();

						if (!bFromWearables)
						{
							// Only needed if the item wasn't being worn already
							OnWearableEquipped(toStack);
						}

						return true;
					}
				}
			}
			else
			{
				if (toStack->count == 0)
				{
					if (bFromWearables)
					{
						OnWearableUnequipped(fromStack);
					}

					toStack->prefabID = fromStack->prefabID;
					toStack->count = fromStack->count;
					toStack->userData = fromStack->userData;
					fromStack->Clear();
					return true;
				}
				else if (toStack->prefabID == fromStack->prefabID &&
					(u32)(toStack->count + fromStack->count) <= g_ResourceManager->GetMaxStackSize(toStack->prefabID))
				{
					if (bFromWearables)
					{
						OnWearableUnequipped(fromStack);
					}

					toStack->count = toStack->count + fromStack->count;
					fromStack->Clear();
					return true;
				}
			}
		}

		return false;
	}

	bool Player::MoveSingleItemFromStack(GameObjectStackID fromID, GameObjectStackID toID)
	{
		InventoryType fromInventoryType, toInventoryType;
		GameObjectStack* fromStack = GetGameObjectStackFromInventory(fromID, fromInventoryType);
		GameObjectStack* toStack = GetGameObjectStackFromInventory(toID, toInventoryType);

		if (fromStack != nullptr && toStack != nullptr && fromStack != toStack)
		{
			bool bFromWearables = fromInventoryType == InventoryType::WEARABLES;
			bool bToWearables = toInventoryType == InventoryType::WEARABLES;

			if (bToWearables)
			{
				GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(fromStack->prefabID);
				if (prefabTemplate->IsWearable())
				{
					if (toStack->count == 0 && fromStack->count >= 1)
					{
						toStack->prefabID = fromStack->prefabID;
						toStack->count = 1;
						toStack->userData = fromStack->userData;
						--fromStack->count;
						if (fromStack->count == 0)
						{
							fromStack->Clear();
						}

						if (!bFromWearables)
						{
							// Only needed if the item wasn't being worn already
							OnWearableEquipped(toStack);
						}

						return true;
					}
				}
			}
			else
			{
				if (toStack->count == 0)
				{
					if (bFromWearables)
					{
						OnWearableUnequipped(fromStack);
					}

					toStack->prefabID = fromStack->prefabID;
					toStack->count = 1;
					toStack->userData = fromStack->userData;
					--fromStack->count;
					if (fromStack->count == 0)
					{
						fromStack->Clear();
					}
					return true;
				}
				else if (toStack->prefabID == fromStack->prefabID &&
					(u32)(toStack->count + 1) <= g_ResourceManager->GetMaxStackSize(toStack->prefabID))
				{
					if (bFromWearables)
					{
						OnWearableUnequipped(fromStack);
					}

					++toStack->count;
					--fromStack->count;
					if (fromStack->count == 0)
					{
						fromStack->Clear();
					}
					return true;
				}
			}
		}

		return false;
	}

	bool Player::DropItemStack(GameObjectStackID stackID, bool bDestroyItem)
	{
		InventoryType inventoryType;
		GameObjectStack* stack = GetGameObjectStackFromInventory(stackID, inventoryType);

		if (stack != nullptr)
		{
			bool bFromWearables = inventoryType == InventoryType::WEARABLES;

			if (bFromWearables)
			{
				OnWearableUnequipped(stack);
			}

			if (!bDestroyItem)
			{
				CreateDroppedItemFromStack(stack);
			}

			stack->Clear();
			return true;
		}

		return false;
	}

	bool Player::DropSingleItemFromStack(GameObjectStackID stackID, bool bDestroyItem)
	{
		InventoryType inventoryType;
		GameObjectStack* stack = GetGameObjectStackFromInventory(stackID, inventoryType);

		if (stack != nullptr && stack->count > 0)
		{
			bool bFromWearables = inventoryType == InventoryType::WEARABLES;

			if (bFromWearables)
			{
				OnWearableUnequipped(stack);
			}

			if (!bDestroyItem)
			{
				CreateDroppedItemFromStack(stack);
			}

			--stack->count;
			if (stack->count == 0)
			{
				stack->Clear();
			}
			return true;
		}

		return false;
	}

	i32 Player::MoveStackBetweenInventories(GameObjectStackID stackID, InventoryType destInventoryType, i32 countToMove)
	{
		InventoryType sourceInventoryType;
		GameObjectStack* sourceStack = GetGameObjectStackFromInventory(stackID, sourceInventoryType);
		if (sourceInventoryType != destInventoryType && sourceStack != nullptr)
		{
			if (countToMove == -1)
			{
				countToMove = sourceStack->count;
			}
			u32 itemsNotMoved = AddToInventory(sourceStack->prefabID, countToMove, sourceStack->userData, destInventoryType);
			if (itemsNotMoved == 0)
			{
				sourceStack->count -= countToMove;
				if (sourceStack->count == 0)
				{
					sourceStack->Clear();
				}
				return 0;
			}
			else
			{
				sourceStack->count = itemsNotMoved;
				return itemsNotMoved;
			}
		}

		return countToMove;
	}

	void Player::CreateDroppedItemFromStack(GameObjectStack* stack)
	{
		glm::vec3 lookDir = GetLookDirection();
		glm::vec3 dropPos = m_Transform.GetWorldPosition() + lookDir * m_ItemDropPosForwardOffset;
		g_SceneManager->CurrentScene()->CreateDroppedItem(
			stack->prefabID,
			stack->count,
			dropPos,
			lookDir * m_ItemDropForwardVelocity);
	}

	GameObjectStackID Player::GetGameObjectStackIDForInventory(u32 slotIndex)
	{
		if (slotIndex <= INVENTORY_ITEM_COUNT)
		{
			return (GameObjectStackID)(slotIndex + INVENTORY_MIN);
		}
		return InvalidID;
	}

	GameObjectStackID Player::GetGameObjectStackIDForQuickAccessInventory(u32 slotIndex)
	{
		if (slotIndex <= QUICK_ACCESS_ITEM_COUNT)
		{
			return (GameObjectStackID)slotIndex + INVENTORY_QUICK_ACCESS_MIN;
		}
		return InvalidID;
	}

	GameObjectStackID Player::GetGameObjectStackIDForWearablesInventory(u32 slotIndex)
	{
		if (slotIndex <= WEARABLES_ITEM_COUNT)
		{
			return (GameObjectStackID)(slotIndex + INVENTORY_WEARABLES_MIN);
		}
		return InvalidID;
	}

	GameObjectStackID Player::GetGameObjectStackIDForMinerInventory(u32 slotIndex)
	{
		if (slotIndex <= Miner::INVENTORY_SIZE)
		{
			return (GameObjectStackID)(slotIndex + INVENTORY_MINER_MIN);
		}
		return InvalidID;
	}

	void Player::AddToInventory(DroppedItem* droppedItem)
	{
		GameObjectStack::UserData userData = {};
		AddToInventory(droppedItem->prefabID, droppedItem->stackSize, userData);

		droppedItem->OnPickedUp();

		g_SceneManager->CurrentScene()->RemoveObject(droppedItem, true);
	}

	void Player::AddToInventory(const PrefabID& prefabID, i32 count)
	{
		AddToInventory(prefabID, count, {});
	}

	void Player::AddToInventory(const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData)
	{
		i32 maxStackSize = g_ResourceManager->GetMaxStackSize(prefabID);

		// Fill up any existing slots in quick access
		for (GameObjectStack& gameObjectStack : m_QuickAccessInventory)
		{
			if (gameObjectStack.prefabID == prefabID && (gameObjectStack.count + 1) <= maxStackSize)
			{
				i32 deposit = glm::min(((i32)maxStackSize - gameObjectStack.count), count);
				count -= deposit;
				gameObjectStack.count += deposit;
				// TODO: Merge user data here
			}

			if (count == 0)
			{
				return;
			}
		}

		// Fill up any existing slots in main inventory
		for (GameObjectStack& gameObjectStack : m_Inventory)
		{
			if (gameObjectStack.prefabID == prefabID && (gameObjectStack.count + 1) <= maxStackSize)
			{
				i32 deposit = glm::min(((i32)maxStackSize - gameObjectStack.count), count);
				count -= deposit;
				gameObjectStack.count += deposit;
				// TODO: Merge user data here
			}

			if (count == 0)
			{
				return;
			}
		}

		// Fill empty slots in quick access
		for (GameObjectStack& gameObjectStack : m_QuickAccessInventory)
		{
			if (gameObjectStack.count == 0)
			{
				i32 deposit = glm::min((i32)maxStackSize, count);
				count -= deposit;
				gameObjectStack.prefabID = prefabID;
				gameObjectStack.count = deposit;
				gameObjectStack.userData = userData;
			}

			if (count == 0)
			{
				return;
			}
		}

		// Fill empty slots in main inventory
		for (GameObjectStack& gameObjectStack : m_Inventory)
		{
			if (gameObjectStack.count == 0)
			{
				i32 deposit = glm::min((i32)maxStackSize, count);
				count -= deposit;
				gameObjectStack.prefabID = prefabID;
				gameObjectStack.count = deposit;
				gameObjectStack.userData = userData;
			}

			if (count == 0)
			{
				return;
			}
		}
	}

	u32 Player::AddToInventory(const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData, InventoryType inventoryType)
	{
		switch (inventoryType)
		{
		case InventoryType::QUICK_ACCESS:
			return MoveToInventory(&m_QuickAccessInventory[0], (u32)m_QuickAccessInventory.size(), prefabID, count, userData);
		case InventoryType::PLAYER_INVENTORY:
			return MoveToInventory(&m_Inventory[0], (u32)m_Inventory.size(), prefabID, count, userData);
		case InventoryType::WEARABLES:
			return MoveToInventory(&m_WearablesInventory[0], (u32)m_WearablesInventory.size(), prefabID, count, userData);
		case InventoryType::MINER_INVENTORY:
		{
			if (m_NearbyInteractableID.IsValid())
			{
				GameObject* nearbyInteractable = m_NearbyInteractableID.Get();
				if (nearbyInteractable->GetTypeID() == MinerSID)
				{
					Miner* miner = (Miner*)nearbyInteractable;
					return miner->AddToInventory(prefabID, count, userData);
				}
			}
		} break;
		}

		return count;
	}

	u32 Player::MoveToInventory(GameObjectStack* inventory, u32 inventorySize, const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData)
	{
		i32 maxStackSize = g_ResourceManager->GetMaxStackSize(prefabID);

		// Fill up any existing slots
		for (u32 i = 0; i < inventorySize; ++i)
		{
			if (inventory[i].prefabID == prefabID && (inventory[i].count + 1) <= maxStackSize)
			{
				i32 deposit = glm::min((maxStackSize - inventory[i].count), count);
				count -= deposit;
				inventory[i].count += deposit;
				// TODO: Merge user data here
			}

			if (count == 0)
			{
				return 0;
			}
		}

		// Fill empty slots
		for (u32 i = 0; i < inventorySize; ++i)
		{
			if (inventory[i].count == 0)
			{
				i32 deposit = glm::min(maxStackSize, count);
				count -= deposit;
				inventory[i].prefabID = prefabID;
				inventory[i].count = deposit;
				inventory[i].userData = userData;
			}

			if (count == 0)
			{
				return 0;
			}
		}

		return count;
	}

	void Player::ClearInventory()
	{
		m_Inventory.fill({});
		m_QuickAccessInventory.fill({});
		m_WearablesInventory.fill({});
	}

	void Player::ParseInventoryFile()
	{
		if (FileExists(USER_INVENTORY_LOCATION))
		{
			std::string fileContents;
			if (ReadFile(USER_INVENTORY_LOCATION, fileContents, false))
			{
				JSONObject inventoryObj;
				if (JSONParser::Parse(fileContents, inventoryObj))
				{
					std::vector<JSONObject> slotLists[3];

					inventoryObj.TryGetObjectArray("slots", slotLists[0]);
					inventoryObj.TryGetObjectArray("quick access slots", slotLists[1]);
					inventoryObj.TryGetObjectArray("wearable slots", slotLists[2]);

					for (i32 slotListIndex = 0; slotListIndex < ARRAY_LENGTH(slotLists); ++slotListIndex)
					{
						for (JSONObject& slot : slotLists[slotListIndex])
						{
							i32 index = slot.GetInt("index");
							i32 count = slot.GetInt("count");
							PrefabID prefabID = slot.GetPrefabID("prefab id");
							i32 maxStackSize = (i32)g_ResourceManager->GetMaxStackSize(prefabID);

							bool bIndexValid = index >= 0 && index < INVENTORY_ITEM_COUNT;
							bool bCountValid = count >= 0 && count <= maxStackSize;
							bool bPrefabIDValid = g_ResourceManager->IsPrefabIDValid(prefabID);

							if (bIndexValid && bCountValid && bPrefabIDValid)
							{
								if (slotListIndex == 0)
								{
									m_Inventory[index] = GameObjectStack(prefabID, count);
								}
								else if (slotListIndex == 1)
								{
									m_QuickAccessInventory[index] = GameObjectStack(prefabID, count);
								}
								else if (slotListIndex == 2)
								{
									m_WearablesInventory[index] = GameObjectStack(prefabID, count);
								}
							}
							else
							{
								if (!bIndexValid)
								{
									PrintError("Invalid user inventory index: %i\n", index);
								}
								if (!bCountValid)
								{
									PrintError("Invalid user inventory count: %i\n", count);
								}
								if (!bPrefabIDValid)
								{
									std::string prefabIDStr = prefabID.ToString();
									PrintError("Invalid user inventory prefabID: %s\n", prefabIDStr.c_str());
								}
							}
						}
					}

					for (const GameObjectStack& stack : m_WearablesInventory)
					{
						if (stack.count > 0)
						{
							OnWearableEquipped(&stack);
						}
					}
				}
				else
				{
					PrintError("Failed to parse user inventory file, error: %s\n", JSONParser::GetErrorString());
				}
			}
			else
			{
				PrintError("Failed to read use inventory file at %s\n", USER_INVENTORY_LOCATION);
			}
		}
		// TODO: Serialize parent & wire reference once ObjectIDs are in

	}

	void Player::SerializeInventoryToFile()
	{
		JSONObject inventoryObj = {};

		auto SerializeSlot = [](i32 slotIdx, GameObjectStack& stack)
		{
			JSONObject slot = {};
			slot.fields.emplace_back("index", JSONValue(slotIdx));
			slot.fields.emplace_back("count", JSONValue(stack.count));
			slot.fields.emplace_back("prefab id", JSONValue(stack.prefabID));

			return slot;
		};

		std::vector<JSONObject> slots;
		if (SerializeInventory((GameObjectStack*)&m_Inventory[0], (u32)m_Inventory.size(), slots))
		{
			inventoryObj.fields.emplace_back("slots", JSONValue(slots));
		}

		std::vector<JSONObject> quickAccessSlots;
		if (SerializeInventory((GameObjectStack*)&m_QuickAccessInventory[0], (u32)m_QuickAccessInventory.size(), quickAccessSlots))
		{
			inventoryObj.fields.emplace_back("quick access slots", JSONValue(quickAccessSlots));
		}

		std::vector<JSONObject> wearablesSlots;
		if (SerializeInventory((GameObjectStack*)&m_WearablesInventory[0], (u32)m_WearablesInventory.size(), wearablesSlots))
		{
			inventoryObj.fields.emplace_back("wearable slots", JSONValue(wearablesSlots));
		}

		Platform::CreateDirectoryRecursive(RelativePathToAbsolute(SAVE_FILE_DIRECTORY));

		std::string fileContents = inventoryObj.ToString();
		if (WriteFile(USER_INVENTORY_LOCATION, fileContents, false))
		{
			Print("Saved user inventory to disk.\n");
		}
		else
		{
			PrintError("Failed to write user inventory to file at %s\n", USER_INVENTORY_LOCATION);
		}
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

	void Player::OnWearableEquipped(GameObjectStack const* wearableStack)
	{
		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(wearableStack->prefabID);
		CHECK(prefabTemplate->IsWearable());

		switch (prefabTemplate->GetTypeID())
		{
		case HeadLampSID:
		{
			GameObject* headLamp = prefabTemplate->CopySelf(this, GameObject::ALL);
			headLamp->GetTransform()->SetLocalPosition(HEADLAMP_MOUNT_POS);
		} break;
		default:
		{
			PrintError("Unhandled wearable equipped\n");
		} break;
		}
	}

	void Player::OnWearableUnequipped(GameObjectStack const* wearableStack)
	{
		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(wearableStack->prefabID);
		CHECK(prefabTemplate->IsWearable());

		switch (prefabTemplate->GetTypeID())
		{
		case HeadLampSID:
		{
			std::vector<HeadLamp*> headlamps;
			GetChildrenOfType<HeadLamp>(HeadLampSID, true, headlamps);
			if (headlamps.size() == 1)
			{
				if (!RemoveChildImmediate(headlamps[0]->ID, true))
				{
					PrintError("Failed to remove headlamp from player\n");
				}
			}
			else
			{
				PrintError("Failed to find headlamp child in player\n");
			}
		} break;
		default:
		{
			PrintError("Unhandled wearable unequipped\n");
		} break;
		}
	}

	bool Player::IsAnyInventoryShowing() const
	{
		return m_bInventoryShowing || m_bMinerInventoryShowing;
	}

	bool Player::IsInventoryShowing() const
	{
		return m_bInventoryShowing;
	}

	bool Player::IsMinerInventoryShowing() const
	{
		return m_bMinerInventoryShowing;
	}

	void Player::ClearTrackBuildingState()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

		if (!ctx.m_GrabbedPoints.empty())
		{
			ctx.m_GrabbedPoints.clear();
			ctx.m_GrabbedTrackEndDirs.clear();
			trackManager->FindJunctions();
		}
		ctx.m_PlacedNodes.clear();
		ctx.m_StartDir = VEC3_ZERO;
		ctx.m_HoveredPoint = {};
		ctx.m_HoveredPlacedNodeIndex = -1;
		ctx.m_HoveredTrackID = InvalidTrackID;
		ctx.m_bReticleValid = false;

		trackManager->SetPreviewTrack(nullptr);
		trackManager->SetHighlightedTrack(InvalidTrackID);
	}

	void Player::UpdateTrackBuildingReticle()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

		ctx.m_bReticleValid = false;
		ctx.m_HoveredPoint = {};
		ctx.m_HoveredPlacedNodeIndex = -1;

		struct IgnoreSelfRayResultCallback : public btCollisionWorld::ClosestRayResultCallback
		{
			IgnoreSelfRayResultCallback(const btVector3& rayFrom, const btVector3& rayTo, const btCollisionObject* selfObject) :
				ClosestRayResultCallback(rayFrom, rayTo),
				self(selfObject)
			{
			}

			virtual bool needsCollision(btBroadphaseProxy* proxy0) const override
			{
				const btCollisionObject* collisionObject = (const btCollisionObject*)proxy0->m_clientObject;
				if (collisionObject == self || (collisionObject->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE))
				{
					return false;
				}
				return ClosestRayResultCallback::needsCollision(proxy0);
			}

			const btCollisionObject* self;
		};

		// Aim where the camera is looking
		btVector3 rayStart, rayEnd;
		FlexEngine::GenerateRayAtScreenCenter(rayStart, rayEnd, ctx.m_MaxReach);

		btDiscreteDynamicsWorld* world = g_SceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
		IgnoreSelfRayResultCallback rayCallback(rayStart, rayEnd, GetRigidBody()->GetRigidBodyInternal());
		world->rayTest(rayStart, rayEnd, rayCallback);
		if (rayCallback.hasHit())
		{
			ctx.m_ReticlePos = ToVec3(rayCallback.m_hitPointWorld);
			ctx.m_bReticleValid = true;
		}
		else
		{
			// Nothing was hit, fall back to intersecting with the plane the track is being built on
			real planeHeight = ctx.m_PlacedNodes.empty() ? (m_Transform.GetWorldPosition().y - m_Height * 0.5f) : ctx.m_PlacedNodes.back().y;
			glm::vec3 start = ToVec3(rayStart);
			glm::vec3 delta = ToVec3(rayEnd - rayStart);
			if (delta.y < -0.0001f)
			{
				real t = (planeHeight - start.y) / delta.y;
				if (t >= 0.0f && t <= 1.0f)
				{
					ctx.m_ReticlePos = start + delta * t;
					ctx.m_bReticleValid = true;
				}
			}
		}

		if (!ctx.m_bReticleValid)
		{
			return;
		}

		// Snap to nearby nodes
		ctx.m_HoveredPoint = trackManager->GetClosestNode(ctx.m_ReticlePos, ctx.m_SnapThreshold,
			ctx.m_GrabbedPoints.empty() ? nullptr : &ctx.m_GrabbedPoints);

		if (ctx.m_bPlacingTrack)
		{
			// Nodes of the track being placed can be snapped to to close a loop, which needs at least three nodes
			real closestDistSq = ctx.m_HoveredPoint.IsValid() ? glm::distance2(ctx.m_ReticlePos, trackManager->GetPoint(ctx.m_HoveredPoint)) : ctx.m_SnapThreshold * ctx.m_SnapThreshold;
			for (i32 i = 0; i < (i32)ctx.m_PlacedNodes.size() - 2; ++i)
			{
				real distSq = glm::distance2(ctx.m_ReticlePos, ctx.m_PlacedNodes[i]);
				if (distSq < closestDistSq)
				{
					closestDistSq = distSq;
					ctx.m_HoveredPlacedNodeIndex = i;
				}
			}
			if (ctx.m_HoveredPlacedNodeIndex != -1)
			{
				ctx.m_HoveredPoint = {};
				ctx.m_ReticlePos = ctx.m_PlacedNodes[ctx.m_HoveredPlacedNodeIndex];
			}
		}

		if (ctx.m_HoveredPoint.IsValid())
		{
			ctx.m_ReticlePos = trackManager->GetPoint(ctx.m_HoveredPoint);
		}
	}

	void Player::UpdateTrackBuilding()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		CHECK(ctx.m_bPlacingTrack || ctx.m_bEditingTrack);

		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
		DebugRenderer* debugRenderer = g_Renderer->GetDebugRenderer();

		UpdateTrackBuildingReticle();

		const btVector3 nodeCol(0.95f, 0.75f, 0.2f);
		const btVector3 highlightCol(1.0f, 1.0f, 1.0f);

		// Nodes lie at ground level, inside the track mesh, so mark them above it
		auto DrawNodeMarker = [debugRenderer](const glm::vec3& node, const btVector3& col)
		{
			const glm::vec3 markerPos = node + VEC3_UP * 1.0f;
			debugRenderer->drawLine(ToBtVec3(node), ToBtVec3(markerPos), col);
			debugRenderer->drawSphere(ToBtVec3(markerPos), 0.25f, col);
		};

		if (ctx.m_bPlacingTrack)
		{
			// Preview the track as it'd be if the next node were placed at the reticle
			std::vector<glm::vec3> previewNodes = ctx.m_PlacedNodes;
			glm::vec3 endDir = VEC3_ZERO;
			if (ctx.m_bReticleValid && !ctx.m_PlacedNodes.empty() && ctx.m_HoveredPlacedNodeIndex == -1 &&
				glm::distance(ctx.m_PlacedNodes.back(), ctx.m_ReticlePos) > 0.01f)
			{
				previewNodes.push_back(ctx.m_ReticlePos);
				if (ctx.m_HoveredPoint.IsValid())
				{
					endDir = trackManager->GetNodeDirection(ctx.m_HoveredPoint);
				}
			}

			if (ctx.m_HoveredPlacedNodeIndex != -1)
			{
				// The loop's tracks are contiguous, so they can be previewed as one
				std::vector<BezierCurveList> loopTracks;
				TrackManager::CreateLoopedTracks(ctx.m_PlacedNodes, ctx.m_StartDir, ctx.m_HoveredPlacedNodeIndex, loopTracks);
				BezierCurveList previewTrack;
				for (const BezierCurveList& loopTrack : loopTracks)
				{
					previewTrack.curves.insert(previewTrack.curves.end(), loopTrack.curves.begin(), loopTrack.curves.end());
				}
				trackManager->SetPreviewTrack(&previewTrack);
			}
			else if (previewNodes.size() >= 2)
			{
				BezierCurveList previewTrack = TrackManager::CreateTrackThroughNodes(previewNodes, ctx.m_StartDir, endDir);
				trackManager->SetPreviewTrack(&previewTrack);
			}
			else
			{
				trackManager->SetPreviewTrack(nullptr);
			}

			for (i32 i = 0; i < (i32)ctx.m_PlacedNodes.size(); ++i)
			{
				DrawNodeMarker(ctx.m_PlacedNodes[i], i == ctx.m_HoveredPlacedNodeIndex ? highlightCol : nodeCol);
			}
		}

		if (ctx.m_bEditingTrack)
		{
			trackManager->SetPreviewTrack(nullptr);

			// Aimed at track will be deleted by the delete action, so show that
			const real hoverRange = 1.25f;
			ctx.m_HoveredTrackID = (ctx.m_GrabbedPoints.empty() && ctx.m_bReticleValid) ? trackManager->GetClosestTrack(ctx.m_ReticlePos, hoverRange) : InvalidTrackID;
			trackManager->SetHighlightedTrack(ctx.m_HoveredTrackID);

			if (!ctx.m_GrabbedPoints.empty() && ctx.m_bReticleValid &&
				glm::distance2(trackManager->GetPoint(ctx.m_GrabbedPoints[0]), ctx.m_ReticlePos) > 0.000001f)
			{
				for (const TrackPointRef& ref : ctx.m_GrabbedPoints)
				{
					trackManager->GetTrack(ref.trackID)->SetPointPosAtIndex(ref.curveIndex, ref.pointIndex, ctx.m_ReticlePos, false);
				}
				// Handles are always generated, keeping each track smooth through its nodes
				for (const GrabbedTrackEndDirs& grabbedTrack : ctx.m_GrabbedTrackEndDirs)
				{
					trackManager->SmoothTrack(grabbedTrack.trackID, grabbedTrack.startDir, grabbedTrack.endDir);
				}
			}
		}

		// Show nearby nodes of existing tracks (to snap to when placing, or to edit)
		const real nodeDrawDistSq = 50.0f * 50.0f;
		const glm::vec3 playerPos = m_Transform.GetWorldPosition();
		for (i32 t = 0; t < (i32)trackManager->tracks.size(); ++t)
		{
			const BezierCurveList& track = trackManager->tracks[t];
			for (i32 c = 0; c < (i32)track.curves.size(); ++c)
			{
				// Curves share nodes, so only draw the end of the last curve
				for (i32 p = 0; p < 4; p += 3)
				{
					if (p == 3 && c != (i32)track.curves.size() - 1)
					{
						continue;
					}

					const glm::vec3& node = track.curves[c].points[p];
					if (glm::distance2(node, playerPos) > nodeDrawDistSq)
					{
						continue;
					}

					TrackPointRef ref = { (TrackID)t, c, p };
					bool bHighlighted = (ref == ctx.m_HoveredPoint) || Contains(ctx.m_GrabbedPoints, ref);
					DrawNodeMarker(node, bHighlighted ? highlightCol : nodeCol);
				}
			}
		}

		if (ctx.m_bReticleValid)
		{
			static const btVector3 placingCol(0.3f, 0.55f, 0.95f);
			static const btVector3 snappedCol(0.3f, 0.95f, 0.45f);
			static const btVector3 editingCol(0.8f, 0.3f, 0.7f);
			static const btVector3 grabbingCol(1.0f, 1.0f, 1.0f);

			btVector3 col = ctx.m_bPlacingTrack ? placingCol : editingCol;
			if (!ctx.m_GrabbedPoints.empty())
			{
				col = grabbingCol;
			}
			else if (ctx.m_HoveredPoint.IsValid())
			{
				col = snappedCol;
			}

			btTransform ringTransform(btQuaternion::getIdentity(), ToBtVec3(ctx.m_ReticlePos + VEC3_UP * 0.05f));
			debugRenderer->drawCylinder(0.5f, 0.01f, 1, ringTransform, col);
			debugRenderer->drawCylinder(0.52f, 0.01f, 1, ringTransform, col);
			debugRenderer->drawLine(ToBtVec3(ctx.m_ReticlePos), ToBtVec3(ctx.m_ReticlePos + VEC3_UP * 1.0f), col);
		}
	}

	void Player::OnTrackBuildingPrimaryAction()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

		if (ctx.m_bPlacingTrack)
		{
			if (!ctx.m_bReticleValid)
			{
				return;
			}

			// Ignore accidental double placements
			if (!ctx.m_PlacedNodes.empty() && glm::distance(ctx.m_PlacedNodes.back(), ctx.m_ReticlePos) < 0.25f)
			{
				return;
			}

			if (ctx.m_HoveredPlacedNodeIndex != -1)
			{
				// Closing a loop finishes the track
				std::vector<BezierCurveList> loopTracks;
				TrackManager::CreateLoopedTracks(ctx.m_PlacedNodes, ctx.m_StartDir, ctx.m_HoveredPlacedNodeIndex, loopTracks);
				for (const BezierCurveList& loopTrack : loopTracks)
				{
					trackManager->AddTrack(loopTrack);
				}
				trackManager->FindJunctions();
				trackManager->SetPreviewTrack(nullptr);

				ctx.m_PlacedNodes.clear();
				ctx.m_StartDir = VEC3_ZERO;
				ctx.m_HoveredPlacedNodeIndex = -1;

				AudioManager::PlaySource(m_SoundPlaceFinalTrackNodeID);
				return;
			}

			const bool bSnappedToTrack = ctx.m_HoveredPoint.IsValid();
			const glm::vec3 snappedDir = bSnappedToTrack ? trackManager->GetNodeDirection(ctx.m_HoveredPoint) : VEC3_ZERO;

			ctx.m_PlacedNodes.push_back(ctx.m_ReticlePos);

			if (ctx.m_PlacedNodes.size() == 1)
			{
				// Starting on an existing track, line up with it
				ctx.m_StartDir = snappedDir;
				AudioManager::PlaySource(m_SoundPlaceTrackNodeID);
			}
			else if (bSnappedToTrack)
			{
				// Joining onto an existing track finishes this one
				AttemptCompleteTrack(snappedDir);
			}
			else
			{
				AudioManager::PlaySource(m_SoundPlaceTrackNodeID);
			}
		}
		else if (ctx.m_bEditingTrack)
		{
			if (!ctx.m_GrabbedPoints.empty())
			{
				ctx.m_GrabbedPoints.clear();
				ctx.m_GrabbedTrackEndDirs.clear();
				trackManager->FindJunctions();
				AudioManager::PlaySource(m_SoundPlaceTrackNodeID);
			}
			else if (ctx.m_HoveredPoint.IsValid())
			{
				// Move every track meeting at this node so junctions stay connected
				trackManager->GetCoincidentNodes(ctx.m_HoveredPoint, ctx.m_GrabbedPoints);

				// Directions where tracks meet others are captured now, as re-reading them every frame would drift while dragging
				ctx.m_GrabbedTrackEndDirs.clear();
				for (const TrackPointRef& ref : ctx.m_GrabbedPoints)
				{
					bool bAlreadyAdded = false;
					for (const GrabbedTrackEndDirs& grabbedTrack : ctx.m_GrabbedTrackEndDirs)
					{
						bAlreadyAdded = bAlreadyAdded || (grabbedTrack.trackID == ref.trackID);
					}
					if (!bAlreadyAdded)
					{
						GrabbedTrackEndDirs grabbedTrack = {};
						grabbedTrack.trackID = ref.trackID;
						trackManager->GetJunctionEndDirs(ref.trackID, grabbedTrack.startDir, grabbedTrack.endDir);
						ctx.m_GrabbedTrackEndDirs.push_back(grabbedTrack);
					}
				}
				AudioManager::PlaySource(m_SoundPlaceTrackNodeID);
			}
		}
	}

	void Player::OnTrackBuildingUndoAction()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);

		if (ctx.m_bPlacingTrack)
		{
			if (!ctx.m_PlacedNodes.empty())
			{
				ctx.m_PlacedNodes.pop_back();
				if (ctx.m_PlacedNodes.empty())
				{
					ctx.m_StartDir = VEC3_ZERO;
				}
				AudioManager::PlaySource(m_SoundTrackDetachID);
			}
		}
		else if (ctx.m_bEditingTrack)
		{
			if (ctx.m_GrabbedPoints.empty() && ctx.m_HoveredPoint.IsValid())
			{
				TrackPointRef nodeToRemove = ctx.m_HoveredPoint;
				ctx.m_HoveredPoint = {};
				trackManager->RemoveNode(nodeToRemove);
				AudioManager::PlaySource(m_SoundTrackDetachID);
			}
		}
	}

	void Player::OnTrackBuildingDeleteTrackAction()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		if (!ctx.m_bEditingTrack || !ctx.m_GrabbedPoints.empty() || ctx.m_HoveredTrackID == InvalidTrackID)
		{
			return;
		}

		TrackID trackToRemove = ctx.m_HoveredTrackID;
		ctx.m_HoveredTrackID = InvalidTrackID;
		GetSystem<TrackManager>(SystemType::TRACK_MANAGER)->RemoveTrack(trackToRemove);
		AudioManager::PlaySource(m_SoundTrackDetachID);
	}

	bool Player::AttemptCompleteTrack(const glm::vec3& endDir /* = VEC3_ZERO */)
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;
		CHECK(ctx.m_bPlacingTrack);

		if (ctx.m_PlacedNodes.size() < 2)
		{
			return false;
		}

		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
		trackManager->AddTrack(TrackManager::CreateTrackThroughNodes(ctx.m_PlacedNodes, ctx.m_StartDir, endDir));
		trackManager->FindJunctions();
		trackManager->SetPreviewTrack(nullptr);

		ctx.m_PlacedNodes.clear();
		ctx.m_StartDir = VEC3_ZERO;

		AudioManager::PlaySource(m_SoundPlaceFinalTrackNodeID);

		return true;
	}

	void Player::ToggleEditingTrack()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;

		if (ctx.m_bPlacingTrack)
		{
			AttemptCompleteTrack();
		}
		ClearTrackBuildingState();

		ctx.m_bEditingTrack = !ctx.m_bEditingTrack;
		ctx.m_bPlacingTrack = false;
	}

	void Player::TogglePlacingTrack()
	{
		TrackBuildingContext& ctx = m_TrackBuildingContext;

		// Leaving build mode keeps whatever was placed
		if (ctx.m_bPlacingTrack)
		{
			AttemptCompleteTrack();
		}
		ClearTrackBuildingState();

		ctx.m_bPlacingTrack = !ctx.m_bPlacingTrack;
		ctx.m_bEditingTrack = false;
	}

	void Player::OnTrackRemoved(TrackID removedTrackID)
	{
		if (m_TrackRidingID != InvalidTrackID)
		{
			if (m_TrackRidingID == removedTrackID)
			{
				DetachFromTrack();
			}
			else if (m_TrackRidingID > removedTrackID)
			{
				--m_TrackRidingID;
			}
		}

		// Point references may now be stale
		m_TrackBuildingContext.m_GrabbedPoints.clear();
		m_TrackBuildingContext.m_GrabbedTrackEndDirs.clear();
		m_TrackBuildingContext.m_HoveredPoint = {};
		m_TrackBuildingContext.m_HoveredTrackID = InvalidTrackID;
	}

	void Player::DrawTrackBuildingHUD() const
	{
		const TrackBuildingContext& ctx = m_TrackBuildingContext;
		const bool bBuilding = ctx.m_bPlacingTrack || ctx.m_bEditingTrack;
		const bool bForkAhead = (m_TrackRidingID != InvalidTrackID) && GetSystem<TrackManager>(SystemType::TRACK_MANAGER)->IsForkAhead();
		if (!m_bPossessed || !(bBuilding || bForkAhead))
		{
			return;
		}

		auto BindingName = [](Action action)
		{
			StringBuilder name;
			if (!g_InputManager->GetActionBindingName(action, name))
			{
				return std::string("Unbound");
			}
			return name.ToString();
		};

		// LMB is checked for directly in PlayerController
		const std::string primaryBinding = "LMB / " + BindingName(Action::INTERACT_RIGHT_HAND);

		std::vector<std::string> lines;
		if (ctx.m_bPlacingTrack)
		{
			lines.push_back("TRACK BUILDING - " + std::to_string(ctx.m_PlacedNodes.size()) + " nodes placed");
			lines.push_back("[" + primaryBinding + "] Place node");
			lines.push_back("[" + BindingName(Action::UNDO_TRACK_NODE) + "] Undo last node");
			lines.push_back("[" + BindingName(Action::COMPLETE_TRACK) + "] Finish track");
			lines.push_back("[" + BindingName(Action::ENTER_TRACK_BUILD_MODE) + "] Finish & exit");
			if (!ctx.m_bReticleValid)
			{
				lines.push_back("Aim at the ground to place nodes");
			}
			else if (ctx.m_HoveredPlacedNodeIndex != -1)
			{
				lines.push_back("Snapped - placing here closes the loop & finishes the track");
			}
			else if (ctx.m_HoveredPoint.IsValid())
			{
				lines.push_back(ctx.m_PlacedNodes.empty() ? "Snapped - new track will branch from this node" : "Snapped - placing here joins & finishes the track");
			}
		}
		else if (ctx.m_bEditingTrack)
		{
			lines.push_back("TRACK EDITING");
			lines.push_back("[" + primaryBinding + "] " + (ctx.m_GrabbedPoints.empty() ? "Grab point" : "Release point"));
			lines.push_back("[" + BindingName(Action::UNDO_TRACK_NODE) + "] Delete hovered node");
			lines.push_back("[" + BindingName(Action::DELETE_TRACK) + "] Delete highlighted track");
			lines.push_back("[" + BindingName(Action::ENTER_TRACK_EDIT_MODE) + "] Exit");
		}
		else
		{
			const real forkSteer = m_Controller->GetForkSteer();
			lines.push_back(std::string("FORK AHEAD - going ") + (forkSteer < 0.0f ? "left" : forkSteer > 0.0f ? "right" : "straight"));
			lines.push_back("[" + BindingName(Action::MOVE_LEFT) + "] / [" + BindingName(Action::MOVE_RIGHT) + "] Choose left / right");
		}

		BitmapFont* font = g_Renderer->SetFont(SID("editor-02"));
		const real lineHeight = 3.2f * font->GetMetric('W')->height / (real)g_Window->GetSize().y;
		const glm::vec4 titleColour(1.0f, 0.85f, 0.4f, 1.0f);
		const glm::vec4 textColour(0.95f, 0.95f, 0.95f, 1.0f);

		// Draw bottom-up so the title sits at the top
		real yOffset = 0.05f;
		for (i32 i = (i32)lines.size() - 1; i >= 0; --i)
		{
			g_Renderer->DrawStringSS(lines[i], i == 0 ? titleColour : textColour, AnchorPoint::BOTTOM_LEFT, glm::vec2(0.03f, yOffset), 1.5f, 0.6f);
			yOffset += lineHeight;
		}
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

	void Player::SetSelectedQuickAccessItemSlot(i32 selectedQuickAccessItemSlot)
	{
		if (m_SelectedQuickAccessItemSlot == selectedQuickAccessItemSlot)
		{
			return;
		}

		m_SelectedQuickAccessItemSlot = selectedQuickAccessItemSlot;
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

	bool Player::IsWieldingPickAxe() const
	{
		const GameObjectStack& stack = m_QuickAccessInventory[m_SelectedQuickAccessItemSlot];
		if (stack.count <= 0 || !stack.prefabID.IsValid())
		{
			return false;
		}

		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(stack.prefabID);
		return prefabTemplate != nullptr && prefabTemplate->GetTypeID() == PickAxeSID;
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
