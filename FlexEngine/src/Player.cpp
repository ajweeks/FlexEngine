#include "stdafx.hpp"

#include "Player.hpp"

#pragma warning(push, 0)
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "BulletCollision/CollisionShapes/btCapsuleShape.h"
#include "BulletDynamics/ConstraintSolver/btHingeConstraint.h"
#pragma warning(pop)

#include "InputManager.hpp"
#include "Physics/RigidBody.hpp"
#include "PlayerController.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	Player::Player(i32 index) :
		GameObject("Player " + std::to_string(index), GameObjectType::PLAYER),
		m_Index(index)
	{
	}

	Player::~Player()
	{
	}

	void Player::Initialize(const GameContext& gameContext)
	{
		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "Player " + std::to_string(m_Index) + " material";
		matCreateInfo.shaderName = "pbr";
		matCreateInfo.constAlbedo = glm::vec3(0.89f, 0.93f, 0.98f);
		matCreateInfo.constMetallic = 0.0f;
		matCreateInfo.constRoughness = 0.98f;
		matCreateInfo.constAO = 1.0f;
		MaterialID matID = gameContext.renderer->InitializeMaterial(&matCreateInfo);

		RigidBody* rigidBody = new RigidBody();
		rigidBody->SetFriction(m_MoveFriction);

		btCapsuleShape* collisionShape = new btCapsuleShape(1.0f, 2.0f);
		
		Material& material = gameContext.renderer->GetMaterial(matID);
		Shader& shader = gameContext.renderer->GetShader(material.shaderID);
		VertexAttributes requiredVertexAttributes = shader.vertexAttributes;

		m_MeshComponent = new MeshComponent(matID, this);
		m_MeshComponent->SetRequiredAttributes(requiredVertexAttributes);
		AddTag("Player" + std::to_string(m_Index));
		SetRigidBody(rigidBody);
		SetStatic(false);
		SetSerializable(false);
		SetCollisionShape(collisionShape);
		m_MeshComponent->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/capsule.gltf");
		m_Transform.SetWorldPosition(glm::vec3(-5.0f + 5.0f * m_Index, 5.0f, 0.0f));

		m_Controller = new PlayerController();
		m_Controller->Initialize(this);


		MaterialCreateInfo slingshotMatCreateInfo = {};
		slingshotMatCreateInfo.name = "Slingshot material";
		slingshotMatCreateInfo.shaderName = "pbr";
		slingshotMatCreateInfo.constAlbedo = glm::vec3(0.5f, 0.25f, 0.02f);
		slingshotMatCreateInfo.constMetallic = 0.0f;
		slingshotMatCreateInfo.constRoughness = 1.0f;
		slingshotMatCreateInfo.constAO = 1.0f;
		MaterialID slingshotMatID = gameContext.renderer->InitializeMaterial(&slingshotMatCreateInfo);

		m_Slingshot = new GameObject("Slingshot", GameObjectType::NONE);
		MeshComponent* slingshotMesh = m_Slingshot->SetMeshComponent(new MeshComponent(slingshotMatID, m_Slingshot));

		Material& slingshotMaterial = gameContext.renderer->GetMaterial(slingshotMatID);
		Shader& slingshotShader = gameContext.renderer->GetShader(slingshotMaterial.shaderID);
		VertexAttributes slingshotRequiredVertexAttributes = slingshotShader.vertexAttributes;
		slingshotMesh->SetRequiredAttributes(slingshotRequiredVertexAttributes);

		slingshotMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/slingshot.gltf");

		AddChild(m_Slingshot);

		m_Slingshot->GetTransform()->SetLocalPosition(glm::vec3(1.0f, 0.0f, 1.0f));

		GameObject::Initialize(gameContext);
	}

	void Player::PostInitialize(const GameContext& gameContext)
	{
		m_RigidBody->GetRigidBodyInternal()->setAngularFactor(btVector3(0, 1, 0));
		m_RigidBody->GetRigidBodyInternal()->setSleepingThresholds(0.0f, 0.0f);

		GameObject::PostInitialize(gameContext);
	}

	void Player::Update(const GameContext& gameContext)
	{
		if (gameContext.inputManager->IsGamepadButtonPressed(m_Index, InputManager::GamepadButton::X))
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
				gameContext.sceneManager->CurrentScene()->GetInteractibleObjects(interactibleObjects);

				if (interactibleObjects.empty())
				{
					p->SetInteractingWith(nullptr);
				}
				else
				{
					GameObject* interactibleObj = nullptr;
					
					for (GameObject* obj : interactibleObjects)
					{
						if (Contains(overlappingObjects, obj) != overlappingObjects.end())
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

		m_Controller->Update(gameContext);

		GameObject::Update(gameContext);
	}

	void Player::Destroy(const GameContext& gameContext)
	{
		if (m_Controller)
		{
			m_Controller->Destroy();
			SafeDelete(m_Controller);
		}

		GameObject::Destroy(gameContext);
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
} // namespace flex
