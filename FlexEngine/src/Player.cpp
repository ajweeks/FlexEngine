#include "stdafx.hpp"

#include "Player.hpp"

#pragma warning(push, 0)
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <glm/gtx/rotate_vector.hpp>
#pragma warning(pop)

#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "Physics/RigidBody.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	Player::Player(i32 index, const glm::vec3& initialPos /* = VEC3_ZERO */) :
		GameObject("Player " + std::to_string(index), GameObjectType::PLAYER),
		m_Index(index)
	{
		m_Transform.SetWorldPosition(initialPos);
	}

	Player::~Player()
	{
	}

	void Player::Initialize()
	{
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
		m_MeshComponent->LoadFromFile(RESOURCE_LOCATION + "meshes/capsule.gltf");

		m_Controller = new PlayerController();
		m_Controller->Initialize(this);


		//MaterialCreateInfo mapTabletMatCreateInfo = {};
		//mapTabletMatCreateInfo.name = "Map tablet material";
		//mapTabletMatCreateInfo.shaderName = "pbr";
		//mapTabletMatCreateInfo.constAlbedo = glm::vec3(0.5f, 0.25f, 0.02f);
		//mapTabletMatCreateInfo.constMetallic = 0.0f;
		//mapTabletMatCreateInfo.constRoughness = 1.0f;
		//mapTabletMatCreateInfo.constAO = 1.0f;
		//MaterialID mapTabletMatID = g_Renderer->InitializeMaterial(&mapTabletMatCreateInfo);
		//
		//m_MapTablet = new GameObject("Map tablet", GameObjectType::NONE);
		//MeshComponent* mapTabletMesh = m_MapTablet->SetMeshComponent(new MeshComponent(mapTabletMatID, m_MapTablet));
		//mapTabletMesh->LoadFromFile(RESOURCE_LOCATION + "meshes/map_tablet.glb");
		//AddChild(m_MapTablet);
		//m_MapTablet->GetTransform()->SetLocalPosition(glm::vec3(-0.75f, -0.3f, 2.3f));
		//m_MapTablet->GetTransform()->SetLocalRotation(glm::quat(glm::vec3(-glm::radians(80.0f), glm::radians(13.30f), -glm::radians(86.0f))));


		m_CrosshairTextureID = g_Renderer->InitializeTexture(RESOURCE_LOCATION + "textures/cross-hair-01.png", 4, false, false, false);

		GameObject::Initialize();
	}

	void Player::PostInitialize()
	{
		m_RigidBody->SetOrientationConstraint(btVector3(0.0f, 1.0f, 0.0f));
		m_RigidBody->GetRigidBodyInternal()->setSleepingThresholds(0.0f, 0.0f);

		GameObject::PostInitialize();
	}

	void Player::Update()
	{
		if (g_InputManager->IsGamepadButtonPressed(m_Index, InputManager::GamepadButton::X))
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

		m_Controller->Update();

		SpriteQuadDrawInfo drawInfo = {};
		drawInfo.anchor = AnchorPoint::CENTER;
		drawInfo.bScreenSpace = true;
		drawInfo.bWriteDepth = false;
		drawInfo.bReadDepth = false;
		drawInfo.scale = glm::vec3(0.02f);
		drawInfo.textureHandleID = g_Renderer->GetTextureHandle(m_CrosshairTextureID);
		g_Renderer->DrawSprite(drawInfo);

		GameObject::Update();
	}

	void Player::Destroy()
	{
		if (m_Controller)
		{
			m_Controller->Destroy();
			SafeDelete(m_Controller);
		}

		GameObject::Destroy();
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

			bool bRiding = (GetController()->GetTrackRiding() != nullptr);
			ImGui::Text("Riding track: %s", (bRiding ? "true" : "false"));
			if (bRiding)
			{
				ImGui::Indent();
				ImGui::Text("Dist along track: %.2f", GetController()->GetDistAlongTrack());
				ImGui::Text("Moving forward down track: %s", (bMovingForwardDownTrack ? "true" : "false"));
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
} // namespace flex
