#include "stdafx.hpp"

#include "Player.hpp"

#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "BulletCollision/CollisionShapes/btCapsuleShape.h"
#include "BulletDynamics/ConstraintSolver/btHingeConstraint.h"

#include "Scene/MeshComponent.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/SceneManager.hpp"
#include "Physics/RigidBody.hpp"
#include "InputManager.hpp"
#include "PlayerController.hpp"

namespace flex
{
	Player::Player(i32 index) :
		GameObject("Player" + std::to_string(index), GameObjectType::PLAYER),
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
		MaterialID matID = gameContext.renderer->InitializeMaterial(gameContext, &matCreateInfo);

		RigidBody* rigidBody = new RigidBody();
		rigidBody->SetFriction(m_MoveFriction);

		btCapsuleShape* collisionShape = new btCapsuleShape(1.0f, 2.0f);
		
		// "Player " + std::to_string(m_Index) + " mesh"
		m_MeshComponent = new MeshComponent(matID, this);
		AddTag("Player" + std::to_string(m_Index));
		SetRigidBody(rigidBody);
		SetStatic(false);
		SetSerializable(false);
		SetCollisionShape(collisionShape);
		m_MeshComponent->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/capsule.gltf");
		m_Transform.SetGlobalPosition(glm::vec3(-5.0f + 5.0f * m_Index, 5.0f, 0.0f));

		m_Controller = new PlayerController();
		m_Controller->Initialize(this);

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

			std::vector<GameObject*> interactibleObjects;
			gameContext.sceneManager->CurrentScene()->GetInteractibleObjects(interactibleObjects);

			if (interactibleObjects.empty())
			{
				p->SetInteractingWith(nullptr);
			}
			else
			{
				GameObject* obj = interactibleObjects[0];
				if (p->SetInteractingWith(obj))
				{
					obj->SetInteractingWithPlayer(true);
				}
				else
				{
					obj->SetInteractingWithPlayer(false);
				}
			}
		}

		if (!m_ObjectInteractingWith)
		{
			m_Controller->Update(gameContext);
		}

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

	bool Player::SetInteractingWith(GameObject* gameObject)
	{
		if (m_ObjectInteractingWith == gameObject)
		{
			m_ObjectInteractingWith = nullptr;
			return false;
		}
		else
		{
			m_ObjectInteractingWith = gameObject;
			return true;
		}
	}
} // namespace flex
