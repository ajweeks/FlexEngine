#include "stdafx.hpp"

#include "Player.hpp"

#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "BulletCollision/CollisionShapes/btCapsuleShape.h"
#include "BulletDynamics/ConstraintSolver/btHingeConstraint.h"

#include "Scene/MeshPrefab.hpp"
#include "Scene/Scenes/BaseScene.hpp"
#include "Scene/SceneManager.hpp"
#include "Physics/RigidBody.hpp"
#include "InputManager.hpp"
#include "PlayerController.hpp"

namespace flex
{
	Player::Player()
	{
	}

	Player::~Player()
	{
	}

	void Player::Initialize(const GameContext& gameContext, i32 index)
	{
		m_Index = index;

		MaterialCreateInfo matCreateInfo = {};
		matCreateInfo.name = "Player " + std::to_string(index) + " material";
		matCreateInfo.shaderName = "pbr";
		matCreateInfo.constAlbedo = glm::vec3(0.89f, 0.93f, 0.98f);
		matCreateInfo.constMetallic = 0.0f;
		matCreateInfo.constRoughness = 0.98f;
		matCreateInfo.constAO = 1.0f;
		MaterialID matID = gameContext.renderer->InitializeMaterial(gameContext, &matCreateInfo);

		RigidBody* rigidBody = new RigidBody();
		rigidBody->SetFriction(m_MoveFriction);

		btCapsuleShape* collisionShape = new btCapsuleShape(1.0f, 2.0f);
		
		m_Mesh = new MeshPrefab(matID, "Player " + std::to_string(index) + " mesh");
		m_Mesh->AddTag("Player" + std::to_string(index));
		m_Mesh->SetRigidBody(rigidBody);
		m_Mesh->SetStatic(false);
		m_Mesh->SetSerializable(false);
		m_Mesh->SetCollisionShape(collisionShape);
		m_Mesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/capsule.gltf");
		m_Mesh->GetTransform()->SetGlobalPosition(glm::vec3(-5.0f + 5.0f * index, 5.0f, 0.0f));
		gameContext.sceneManager->CurrentScene()->AddChild(m_Mesh);
		
		m_Controller = new PlayerController();
		m_Controller->Initialize(this);
	}

	void Player::PostInitialize(const GameContext& gameContext)
	{
		m_Mesh->GetRigidBody()->GetRigidBodyInternal()->setAngularFactor(btVector3(0, 1, 0));
		m_Mesh->GetRigidBody()->GetRigidBodyInternal()->setSleepingThresholds(0.0f, 0.0f);
	}

	void Player::Update(const GameContext& gameContext)
	{
		m_Controller->Update(gameContext);
	}

	void Player::Destroy(const GameContext& gameContext)
	{
		if (m_Controller)
		{
			m_Controller->Destroy();
			SafeDelete(m_Controller);
		}
	}

	i32 Player::GetIndex() const
	{
		return m_Index;
	}

	RigidBody* Player::GetRigidBody()
	{
		return m_Mesh->GetRigidBody();
	}
} // namespace flex
