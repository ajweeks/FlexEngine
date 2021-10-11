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
#include "JSONParser.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "PlayerController.hpp"
#include "ResourceManager.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/TrackManager.hpp"

namespace flex
{
	const glm::vec3 Player::HeadlampMountingPos = glm::vec3(0.0f, 0.8f, 0.2f);

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
		matCreateInfo.constAlbedo = glm::vec4(0.89f, 0.93f, 0.98f, 1.0f);
		matCreateInfo.constMetallic = 0.0f;
		matCreateInfo.constRoughness = 0.98f;
		matCreateInfo.bSerializable = false;
		MaterialID matID = g_Renderer->InitializeMaterial(&matCreateInfo);

		RigidBody* rigidBody = new RigidBody((i32)CollisionType::DEFAULT, (i32)CollisionType::STATIC | (i32)CollisionType::DEFAULT);
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
			mapTabletMatCreateInfo.constAlbedo = glm::vec4(0.34f, 0.38f, 0.39f, 1.0f);
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

		ParseInventoryFile();

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
		if (m_Controller != nullptr)
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

		if (m_bTabletUp)
		{
			m_TabletOrbitAngle = MoveTowards(m_TabletOrbitAngle, m_TabletOrbitAngleUp, g_DeltaTime * 10.0f);
		}
		else
		{
			m_TabletOrbitAngle = MoveTowards(m_TabletOrbitAngle, m_TabletOrbitAngleDown, g_DeltaTime * 10.0f);
		}
		m_MapTabletHolder->GetTransform()->SetLocalRotation(glm::quat(glm::vec3(0.0f, glm::radians(m_TabletOrbitAngle), 0.0f)));

		if (terminalInteractingWithID.IsValid() && g_EngineInstance->IsRenderingImGui())
		{
			Terminal* terminal = (Terminal*)terminalInteractingWithID.Get();
			terminal->DrawImGuiWindow();
		}

		if (ridingVehicleID.IsValid())
		{
			Vehicle* vehicle = (Vehicle*)ridingVehicleID.Get();

			const glm::vec3 posOffset = glm::vec3(0.0f, 3.0f, 0.0f);

			m_Transform.SetWorldPosition(vehicle->GetTransform()->GetWorldPosition() + posOffset);
		}

		BaseScene* scene = g_SceneManager->CurrentScene();
		if (m_Transform.GetWorldPosition().y < scene->GetPlayerMinHeight())
		{
			Reset();
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

	glm::vec3 Player::GetLookDirection()
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

			ImGui::Text("Held item slot: %i", heldItemSlot);

			DrawInventoryImGui("Inventory", -1, m_Inventory);
			DrawInventoryImGui("Quick access inventory", heldItemSlot, m_QuickAccessInventory);
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

	glm::vec3 Player::GetTrackPlacementReticlePosWS(real snapThreshold /* = -1.0f */, bool bSnapToHandles /* = false */)
	{
		glm::vec3 offsetWS = m_TrackPlacementReticlePos;
		glm::mat4 rotMat = glm::mat4(m_Transform.GetWorldRotation());
		offsetWS = rotMat * glm::vec4(offsetWS, 1.0f);

		glm::vec3 point = m_Transform.GetWorldPosition() + offsetWS;

		if (snapThreshold != -1.0f)
		{
			glm::vec3 pointInRange;
			TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
			if (trackManager->GetPointInRange(point, bSnapToHandles, snapThreshold, &pointInRange))
			{
				point = pointInRange;
			}
		}

		return point;
	}

	void Player::AttachToTrack(TrackID trackID, real distAlongTrack)
	{
		TrackManager* trackManager = GetSystem<TrackManager>(SystemType::TRACK_MANAGER);
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

	bool Player::IsRidingTrack()
	{
		return m_TrackRidingID != InvalidTrackID;
	}

	GameObjectStack* Player::GetGameObjectStackFromInventory(GameObjectStackID stackID, InventoryType& outInventoryType)
	{
		if ((i32)stackID < INVENTORY_MAX)
		{
			outInventoryType = InventoryType::INVENTORY;
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
			if (toInventoryType == InventoryType::WEARABLES)
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

						OnWearableEquipped(toStack);

						return true;
					}
				}
			}
			else
			{
				bool bFromWearables = fromInventoryType == InventoryType::WEARABLES;

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
					// TODO: Merge user data's here somehow?
					fromStack->Clear();
					return true;
				}
			}
		}

		return false;
	}

	GameObjectStackID Player::GetGameObjectStackIDForInventory(i32 slotIndex)
	{
		if (slotIndex >= 0 && slotIndex <= INVENTORY_ITEM_COUNT)
		{
			return (GameObjectStackID)(slotIndex + INVENTORY_MIN);
		}
		return InvalidID;
	}

	GameObjectStackID Player::GetGameObjectStackIDForQuickAccessInventory(i32 slotIndex)
	{
		if (slotIndex >= 0 && slotIndex <= QUICK_ACCESS_ITEM_COUNT)
		{
			return (GameObjectStackID)slotIndex + INVENTORY_QUICK_ACCESS_MIN;
		}
		return InvalidID;
	}

	GameObjectStackID Player::GetGameObjectStackIDForWearablesInventory(i32 slotIndex)
	{
		if (slotIndex >= 0 && slotIndex <= WEARABLES_ITEM_COUNT)
		{
			return (GameObjectStackID)(slotIndex + INVENTORY_WEARABLES_MIN);
		}
		return InvalidID;
	}

	void Player::AddToInventory(const PrefabID& prefabID, i32 count)
	{
		AddToInventory(prefabID, count, {});
	}

	void Player::AddToInventory(const PrefabID& prefabID, i32 count, const GameObjectStack::UserData& userData)
	{
		i32 initialCount = count;

		i32 maxStackSize = g_ResourceManager->GetMaxStackSize(prefabID);

		auto printResults = [initialCount, &prefabID, &count]()
		{
			std::string itemName = g_ResourceManager->GetPrefabTemplate(prefabID)->GetName();
			Print("Added %d \"%s\"s to player inventory\n", initialCount - count, itemName.c_str());
		};

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
				printResults();
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
				printResults();
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
				printResults();
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
				printResults();
				return;
			}
		}

		printResults();
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

					for (i32 slotListIndex = 0; slotListIndex < 2; ++slotListIndex)
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
		slots.reserve(m_Inventory.size());
		for (i32 slotIdx = 0; slotIdx < (i32)m_Inventory.size(); ++slotIdx)
		{
			GameObjectStack& stack = m_Inventory[slotIdx];
			if (stack.count > 0)
			{
				slots.emplace_back(SerializeSlot(slotIdx, stack));
			}
		}

		std::vector<JSONObject> quickAccessSlots;
		quickAccessSlots.reserve(m_QuickAccessInventory.size());
		for (i32 slotIdx = 0; slotIdx < (i32)m_QuickAccessInventory.size(); ++slotIdx)
		{
			GameObjectStack& stack = m_QuickAccessInventory[slotIdx];
			if (stack.count > 0)
			{
				quickAccessSlots.emplace_back(SerializeSlot(slotIdx, stack));
			}
		}

		std::vector<JSONObject> wearablesSlots;
		wearablesSlots.reserve(m_WearablesInventory.size());
		for (i32 slotIdx = 0; slotIdx < (i32)m_WearablesInventory.size(); ++slotIdx)
		{
			GameObjectStack& stack = m_WearablesInventory[slotIdx];
			if (stack.count > 0)
			{
				wearablesSlots.emplace_back(SerializeSlot(slotIdx, stack));
			}
		}

		inventoryObj.fields.emplace_back("slots", JSONValue(slots));
		inventoryObj.fields.emplace_back("quick access slots", JSONValue(quickAccessSlots));

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
			assert(!terminalInteractingWithID.IsValid());

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

			terminalInteractingWithID = terminal->ID;
			terminal->SetBeingInteractedWith(this);
		}
		else
		{
			assert(terminalInteractingWithID.IsValid());
			Terminal* terminalInteractingWith = (Terminal*)terminalInteractingWithID.Get();

			BaseCamera* cam = g_CameraManager->CurrentCamera();
			assert(cam->type == CameraType::TERMINAL);
			TerminalCamera* terminalCam = static_cast<TerminalCamera*>(cam);
			terminalCam->SetTerminal(nullptr);

			terminalInteractingWith->SetBeingInteractedWith(nullptr);
			terminalInteractingWithID = InvalidGameObjectID;
		}
	}

	void Player::SetRidingVehicle(Vehicle* vehicle)
	{
		if (vehicle != nullptr)
		{
			assert(!ridingVehicleID.IsValid());

			vehicle->OnPlayerEnter();

			ridingVehicleID = vehicle->ID;

			BaseCamera* cam = g_CameraManager->CurrentCamera();
			VehicleCamera* vehicleCamera = nullptr;
			// Switch to vehicle cam
			if (cam->type != CameraType::VEHICLE)
			{
				vehicleCamera = static_cast<VehicleCamera*>(g_CameraManager->GetCameraByName("vehicle"));
				g_CameraManager->PushCamera(vehicleCamera, false, true);
			}

			SetVisible(false);
		}
		else
		{
			Vehicle* ridingVehicle = (Vehicle*)ridingVehicleID.Get();
			ridingVehicle->OnPlayerExit();

			ridingVehicleID = InvalidGameObjectID;

			BaseCamera* cam = g_CameraManager->CurrentCamera();
			if (cam->type == CameraType::VEHICLE)
			{
				g_CameraManager->PopCamera();
			}

			SetVisible(true);
		}
	}

	bool Player::PickupWithFreeHand(GameObject* object)
	{
		if (!heldItemLeftHand.IsValid())
		{
			heldItemLeftHand = object->ID;
			return true;
		}
		if (!heldItemRightHand.IsValid())
		{
			heldItemRightHand = object->ID;
			return true;
		}
		return false;
	}

	bool Player::IsHolding(GameObject* object)
	{
		return heldItemLeftHand == object->ID || heldItemRightHand == object->ID;
	}

	void Player::DropIfHolding(GameObject* object)
	{
		if (object != nullptr)
		{
			if (heldItemLeftHand == object->ID)
			{
				heldItemLeftHand = InvalidGameObjectID;
			}
			if (heldItemRightHand == object->ID)
			{
				heldItemRightHand = InvalidGameObjectID;
			}
		}
	}

	bool Player::HasFreeHand() const
	{
		return !heldItemLeftHand.IsValid() || !heldItemRightHand.IsValid();
	}

	void Player::OnWearableEquipped(GameObjectStack* wearableStack)
	{
		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(wearableStack->prefabID);
		assert(prefabTemplate->IsWearable());

		switch (prefabTemplate->GetTypeID())
		{
		case SID("headlamp"):
		{
			GameObject* headLamp = prefabTemplate->CopySelf(this, GameObject::ALL);
			headLamp->GetTransform()->SetLocalPosition(HeadlampMountingPos);
		} break;
		default:
		{
			PrintError("Unhandled wearable equipped\n");
		} break;
		}
	}

	void Player::OnWearableUnequipped(GameObjectStack* wearableStack)
	{
		GameObject* prefabTemplate = g_ResourceManager->GetPrefabTemplate(wearableStack->prefabID);
		assert(prefabTemplate->IsWearable());

		switch (prefabTemplate->GetTypeID())
		{
		case SID("headlamp"):
		{
			std::vector<HeadLamp*> headlamps;
			GetChildrenOfType<HeadLamp>(SID("headlamp"), true, headlamps);
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
} // namespace flex
