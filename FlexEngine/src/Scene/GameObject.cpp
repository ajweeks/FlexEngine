#include "stdafx.hpp"

#pragma warning(push, 0)
#include <LinearMath/btTransform.h>
#include <LinearMath/btIDebugDraw.h>
#include <BulletCollision/BroadphaseCollision/btBroadphaseProxy.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletDynamics/ConstraintSolver/btFixedConstraint.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#pragma warning(pop)

#include "Audio/AudioManager.hpp"
#include "GameContext.hpp"
#include "Physics/RigidBody.hpp"
#include "Player.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	RandomizedAudioSource GameObject::s_SqueakySounds;
	AudioSourceID GameObject::s_BunkSound;

	GameObject::GameObject(const std::string& name, GameObjectType type) :
		m_Name(name),
		m_Type(type)
	{
		m_Transform.SetAsIdentity();
		m_Transform.SetGameObject(this);

		if (!s_SqueakySounds.IsInitialized())
		{
			s_SqueakySounds.Initialize(RESOURCE_LOCATION + "audio/squeak00.wav", 5);

			s_BunkSound = AudioManager::AddAudioSource(RESOURCE_LOCATION + "audio/bunk.wav");
		}
	}

	GameObject::~GameObject()
	{
	}

	GameObject* GameObject::CopySelf(const GameContext& gameContext, GameObject* parent, const std::string& newObjectName, bool bCopyChildren)
	{
		GameObject* newGameObject = new GameObject(newObjectName, m_Type);

		CopyGenericFields(gameContext, newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void GameObject::ParseUniqueFields(const GameContext& gameContext, const JSONObject& parentObj, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(gameContext);
		UNREFERENCED_PARAMETER(parentObj);
		UNREFERENCED_PARAMETER(scene);
		UNREFERENCED_PARAMETER(matID);

		// Generic game objects have no unique fields
	}

	void GameObject::SerializeUniqueFields(JSONObject& parentObject)
	{
		UNREFERENCED_PARAMETER(parentObject);

		// Generic game objects have no unique fields
	}

	void GameObject::Initialize(const GameContext& gameContext)
	{
		if (m_RigidBody)
		{
			if (!m_CollisionShape)
			{
				Logger::LogError("Game object contains rigid body but no collision shape! Must call SetCollisionShape before Initialize");
			}
			else
			{
				m_RigidBody->Initialize(m_CollisionShape, gameContext, &m_Transform);
			}
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Initialize(gameContext);
		}
	}

	void GameObject::PostInitialize(const GameContext& gameContext)
	{
		RigidBody* rb = GetRigidBody();

		if (m_RenderID != InvalidRenderID)
		{
			gameContext.renderer->PostInitializeRenderObject(gameContext, m_RenderID);
		}

		if (rb)
		{
			rb->GetRigidBodyInternal()->setUserPointer(this);
		}

		for (auto child : m_Children)
		{
			child->PostInitialize(gameContext);
		}
	}

	void GameObject::Destroy(const GameContext& gameContext)
	{
		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Destroy(gameContext);
			SafeDelete(*iter);
		}
		m_Children.clear();

		if (m_MeshComponent)
		{
			m_MeshComponent->Destroy();
			SafeDelete(m_MeshComponent);
		}

		if (m_RenderID != InvalidRenderID)
		{
			gameContext.renderer->DestroyRenderObject(m_RenderID);
			m_RenderID = InvalidRenderID;
		}

		if (m_RigidBody)
		{
			m_RigidBody->Destroy(gameContext);
			SafeDelete(m_RigidBody);
		}

		// NOTE: SafeDelete does not work on this type
		if (m_CollisionShape)
		{
			delete m_CollisionShape;
			m_CollisionShape = nullptr;
		}
	}

	void GameObject::Update(const GameContext& gameContext)
	{
		if (m_ObjectInteractingWith)
		{
			// TODO: Write real fancy-lookin outline shader instead of drawing a lil cross
			btIDebugDraw* debugDrawer = gameContext.renderer->GetDebugDrawer();
			auto pos = Vec3ToBtVec3(m_Transform.GetWorldPosition());
			debugDrawer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.95f, 0.1f, 0.1f));
			debugDrawer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.95f, 0.1f, 0.1f));
		}
		else if (m_bInteractable)
		{
			btIDebugDraw* debugDrawer = gameContext.renderer->GetDebugDrawer();
			auto pos = Vec3ToBtVec3(m_Transform.GetWorldPosition());
			debugDrawer->drawLine(pos + btVector3(-1, 0.1f, 0), pos + btVector3(1, 0.1f, 0), btVector3(0.95f, 0.95f, 0.1f));
			debugDrawer->drawLine(pos + btVector3(0, 0.1f, -1), pos + btVector3(0, 0.1f, 1), btVector3(0.95f, 0.95f, 0.1f));
		}

		if (m_RigidBody)
		{
			if (m_RigidBody->IsKinematic())
			{
				m_RigidBody->MatchParentTransform();
			}
			else
			{
				// Rigid body will take these changes into account
				m_RigidBody->UpdateParentTransform();
			}
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Update(gameContext);
		}
	}

	bool GameObject::SetInteractingWith(GameObject* gameObject)
	{
		m_ObjectInteractingWith = gameObject;

		bBeingInteractedWith = (gameObject != nullptr);

		return true;
	}

	GameObject* GameObject::GetObjectInteractingWith()
	{
		return m_ObjectInteractingWith;
	}

	GameObjectType GameObject::GetType() const
	{
		return m_Type;
	}

	void GameObject::CopyGenericFields(const GameContext& gameContext, GameObject* newGameObject, GameObject* parent, bool bCopyChildren)
	{
		RenderObjectCreateInfo createInfo = {};
		gameContext.renderer->GetRenderObjectCreateInfo(m_RenderID, createInfo);

		// Make it clear we aren't copying vertex or index data directly
		createInfo.vertexBufferData = nullptr;
		createInfo.indices = nullptr;

		MaterialID matID = createInfo.materialID;
		*newGameObject->GetTransform() = m_Transform;

		if (parent)
		{
			parent->AddChild(newGameObject);
		}
		else
		{
			if (m_Parent)
			{
				m_Parent->AddChild(newGameObject);
			}
			else
			{
				gameContext.sceneManager->CurrentScene()->AddRootObject(newGameObject);
			}
		}

		for (auto tag : m_Tags)
		{
			newGameObject->AddTag(tag);
		}

		if (m_MeshComponent)
		{
			MeshComponent* newMeshComponent = newGameObject->SetMeshComponent(new MeshComponent(matID, newGameObject));
			MeshComponent::Type prefabType = m_MeshComponent->GetType();
			if (prefabType == MeshComponent::Type::PREFAB)
			{
				MeshComponent::PrefabShape shape = m_MeshComponent->GetShape();
				newMeshComponent->LoadPrefabShape(gameContext, shape, &createInfo);
			}
			else if (prefabType == MeshComponent::Type::FILE)
			{
				std::string filePath = m_MeshComponent->GetFilepath();
				MeshComponent::ImportSettings importSettings = m_MeshComponent->GetImportSettings();
				newMeshComponent->LoadFromFile(gameContext, filePath, &importSettings, &createInfo);
			}
			else
			{
				Logger::LogError("Unhandled mesh component prefab type encountered while duplicating object");
			}
		}

		if (m_RigidBody)
		{
			newGameObject->SetRigidBody(new RigidBody(*m_RigidBody));

			btCollisionShape* collisionShape = m_RigidBody->GetRigidBodyInternal()->getCollisionShape();
			newGameObject->SetCollisionShape(collisionShape);

			// TODO: Copy over constraints here
		}

		newGameObject->Initialize(gameContext);
		newGameObject->PostInitialize(gameContext);

		if (bCopyChildren)
		{
			for (GameObject* child : m_Children)
			{
				std::string newChildName = child->GetName();
				GameObject* newChild = child->CopySelf(gameContext, newGameObject, newChildName, bCopyChildren);
				newGameObject->AddChild(newChild);
			}
		}
	}

	GameObject* GameObject::GetParent()
	{
		return m_Parent;
	}

	void GameObject::DetachFromParent()
	{
		if (m_Parent)
		{
			m_Parent->RemoveChild(this);
		}
	}

	void GameObject::SetParent(GameObject* parent)
	{
		if (parent == this)
		{
			Logger::LogError("Attempted to set parent as self! (" + m_Name + ")");
			return;
		}

		glm::mat4 worldTransform = m_Transform.GetWorldTransform();

		m_Parent = parent;

		m_Transform.SetWorldTransform(worldTransform);
	}

	GameObject* GameObject::AddChild(GameObject* child)
	{
		if (!child)
		{
			return nullptr;
		}

		if (child == this)
		{
			Logger::LogError("Attempted to add self as child! (" + m_Name + ")");
			return nullptr;
		}

		Transform* childTransform = child->GetTransform();
		glm::mat4 childWorldTransform = childTransform->GetWorldTransform();

		if (child == m_Parent)
		{
			DetachFromParent();
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if (*iter == child)
			{
				// Don't add the same child twice
				return nullptr;
			}
		}

		m_Children.push_back(child);

		child->SetParent(this);

		childTransform->SetWorldTransform(childWorldTransform);

		return child;
	}

	bool GameObject::RemoveChild(GameObject* child)
	{
		if (!child)
		{
			return false;
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if (*iter == child)
			{
				glm::mat4 childWorldTransform = child->GetTransform()->GetWorldTransform();

				child->SetParent(nullptr);

				child->GetTransform()->SetWorldTransform(childWorldTransform);

				m_Children.erase(iter);
				return true;
			}
		}

		return false;
	}

	void GameObject::RemoveAllChildren()
	{
		auto iter = m_Children.begin();
		while (iter != m_Children.end())
		{
			iter = m_Children.erase(iter);
		}
		m_Children.clear();
	}

	const std::vector<GameObject*>& GameObject::GetChildren() const
	{
		return m_Children;
	}

	bool GameObject::HasChild(GameObject* child, bool bRecurse)
	{
		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			if (*iter == child)
			{
				return true;
			}

			if (bRecurse && 
				*iter && 
				(*iter)->HasChild(child, bRecurse))
			{
				return true;
			}
		}
		return false;
	}

	Transform* GameObject::GetTransform()
	{
		return &m_Transform;
	}

	void GameObject::AddTag(const std::string& tag)
	{
		if (std::find(m_Tags.begin(), m_Tags.end(), tag) == m_Tags.end())
		{
			m_Tags.push_back(tag);
		}
	}

	bool GameObject::HasTag(const std::string& tag)
	{
		auto result = std::find(m_Tags.begin(), m_Tags.end(), tag);
		return (result != m_Tags.end());
	}

	std::vector<std::string> GameObject::GetTags() const
	{
		return m_Tags;
	}

	RenderID GameObject::GetRenderID() const
	{
		return m_RenderID;
	}

	std::string GameObject::GetName() const
	{
		return m_Name;
	}

	void GameObject::SetName(const std::string& newName)
	{
		m_Name = newName;
	}

	void GameObject::SetRenderID(RenderID renderID)
	{
		m_RenderID = renderID;
	}

	bool GameObject::IsSerializable() const
	{
		return m_bSerializable;
	}

	void GameObject::SetSerializable(bool bSerializable)
	{
		m_bSerializable = bSerializable;
	}

	bool GameObject::IsStatic() const
	{
		return m_bStatic;
	}

	void GameObject::SetStatic(bool bStatic)
	{
		m_bStatic = bStatic;
	}

	bool GameObject::IsVisible() const
	{
		return m_bVisible;
	}

	void GameObject::SetVisible(bool bVisible, bool effectChildren)
	{
		if (m_bVisible != bVisible)
		{
			m_bVisible = bVisible;

			if (effectChildren)
			{
				for (GameObject* child : m_Children)
				{
					child->SetVisible(bVisible, effectChildren);
				}
			}
		}
	}

	bool GameObject::IsVisibleInSceneExplorer() const
	{
		return m_bVisibleInSceneExplorer;
	}

	void GameObject::SetVisibleInSceneExplorer(bool bVisibleInSceneExplorer)
	{
		if (m_bVisibleInSceneExplorer != bVisibleInSceneExplorer)
		{
			m_bVisibleInSceneExplorer = bVisibleInSceneExplorer;
		}
	}

	btCollisionShape* GameObject::SetCollisionShape(btCollisionShape* collisionShape)
	{
		if (m_CollisionShape)
		{
			delete m_CollisionShape;
			m_CollisionShape = nullptr;
		}

		m_CollisionShape = collisionShape;
		return collisionShape;
	}

	btCollisionShape* GameObject::GetCollisionShape() const
	{
		return m_CollisionShape;
	}

	RigidBody* GameObject::SetRigidBody(RigidBody* rigidBody)
	{
		SafeDelete(m_RigidBody);

		m_RigidBody = rigidBody;
		return rigidBody;
	}

	RigidBody* GameObject::GetRigidBody() const
	{
		return m_RigidBody;
	}

	MeshComponent* GameObject::GetMeshComponent()
	{
		return m_MeshComponent;
	}

	MeshComponent* GameObject::SetMeshComponent(MeshComponent* meshComponent)
	{
		m_MeshComponent = meshComponent;
		return meshComponent;
	}

	void GameObject::OnOverlapBegin(GameObject* other)
	{
		overlappingObjects.push_back(other);

		if (m_Type != GameObjectType::PLAYER)
		{
			if (other->HasTag("Player0") ||
				other->HasTag("Player1"))
			{
				m_bInteractable = true;
			}
		}
	}

	void GameObject::OnOverlapEnd(GameObject* other)
	{
		for (auto iter = overlappingObjects.begin(); iter != overlappingObjects.end(); /* */)
		{
			if (*iter == other)
			{
				iter = overlappingObjects.erase(iter);
			}
			else
			{
				++iter;
			}
		}

		switch (m_Type)
		{
		case GameObjectType::PLAYER:
		{

		} break;
		default: // All non-player objects
		{
			if (other->HasTag("Player0") ||
				other->HasTag("Player1"))
			{
				m_bInteractable = false;
			}
		} break;
		}
	}

	Valve::Valve(const std::string& name) :
		GameObject(name, GameObjectType::VALVE)
	{
	}

	GameObject* Valve::CopySelf(const GameContext& gameContext, GameObject* parent, const std::string& newObjectName, bool bCopyChildren)
	{
		Valve* newGameObject = new Valve(newObjectName);

		newGameObject->minRotation = minRotation;
		newGameObject->maxRotation = maxRotation;
		newGameObject->rotationSpeedScale = rotationSpeedScale;
		newGameObject->invSlowDownRate = invSlowDownRate;
		newGameObject->rotationSpeed = rotationSpeed;
		newGameObject->rotation = rotation;

		CopyGenericFields(gameContext, newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void Valve::ParseUniqueFields(const GameContext& gameContext, const JSONObject& parentObj, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);

		JSONObject valveInfo;
		if (parentObj.SetObjectChecked("valve info", valveInfo))
		{
			glm::vec2 valveRange;
			valveInfo.SetVec2Checked("range", valveRange);
			minRotation = valveRange.x;
			maxRotation = valveRange.y;
			if (glm::abs(maxRotation - minRotation) <= 0.0001f)
			{
				Logger::LogWarning("Valve's rotation range is 0, it will not be able to rotate!");
			}
			if (minRotation > maxRotation)
			{
				Logger::LogWarning("Valve's minimum rotation range is greater than its maximum! Undefined behavior");
			}

			if (!m_MeshComponent)
			{
				VertexAttributes requiredVertexAttributes = 0;
				if (matID != InvalidMaterialID)
				{
					Material& material = gameContext.renderer->GetMaterial(matID);
					Shader& shader = gameContext.renderer->GetShader(material.shaderID);
					requiredVertexAttributes = shader.vertexAttributes;
				}

				MeshComponent* valveMesh = new MeshComponent(matID, this);
				valveMesh->SetRequiredAttributes(requiredVertexAttributes);
				valveMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/valve.gltf");
				assert(GetMeshComponent() == nullptr);
				SetMeshComponent(valveMesh);
			}

			if (!m_CollisionShape)
			{
				btVector3 btHalfExtents(1.5f, 1.0f, 1.5f);
				btCylinderShape* cylinderShape = new btCylinderShape(btHalfExtents);

				SetCollisionShape(cylinderShape);
			}

			if (!m_RigidBody)
			{
				RigidBody* rigidBody = SetRigidBody(new RigidBody());
				rigidBody->SetMass(1.0f);
				rigidBody->SetKinematic(false);
				rigidBody->SetStatic(false);
			}
		}
		else
		{
			Logger::LogWarning("Valve's \"valve info\" field missing!");
		}
	}

	void Valve::SerializeUniqueFields(JSONObject& parentObject)
	{
		JSONObject valveInfo = {};

		glm::vec2 valveRange(minRotation, maxRotation);
		valveInfo.fields.push_back(JSONField("range", JSONValue(Vec2ToString(valveRange))));

		parentObject.fields.push_back(JSONField("valve info", JSONValue(valveInfo)));
	}

	void Valve::PostInitialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);

		m_RigidBody->SetPhysicsFlags((u32)PhysicsFlag::TRIGGER);
		auto rbInternal = m_RigidBody->GetRigidBodyInternal();
		rbInternal->setAngularFactor(btVector3(0, 1, 0));
		// Mark as trigger
		rbInternal->setCollisionFlags(rbInternal->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
		rbInternal->setGravity(btVector3(0, 0, 0));
	}

	void Valve::Update(const GameContext& gameContext)
	{
		// True when our rotation is changed by another object (rising block)
		bool bRotatedByOtherObject = false;
		real currentAbsAvgRotationSpeed = 0.0f;
		if (m_ObjectInteractingWith)
		{
			i32 playerIndex = ((Player*)m_ObjectInteractingWith)->GetIndex();

			const InputManager::GamepadState& gamepadState = gameContext.inputManager->GetGamepadState(playerIndex);
			rotationSpeed = (-gamepadState.averageRotationSpeeds.currentAverage) * rotationSpeedScale;
			currentAbsAvgRotationSpeed = glm::abs(gamepadState.averageRotationSpeeds.currentAverage);
		}
		else
		{
			rotationSpeed = (rotation - pRotation) / gameContext.deltaTime;
			// Not entirely true but needed to trigger sound
			currentAbsAvgRotationSpeed = glm::abs(rotationSpeed);
			bRotatedByOtherObject = (glm::abs(rotation - pRotation) > 0);
		}

		if ((rotationSpeed < 0.0f &&
			rotation <= minRotation) ||
			(rotationSpeed > 0.0f &&
			rotation >= maxRotation))
		{
			rotationSpeed = 0.0f;
			pRotationSpeed = 0.0f;
		}
		else
		{
			if (rotationSpeed == 0.0f)
			{
				pRotationSpeed *= invSlowDownRate;
			}
			else
			{
				pRotationSpeed = rotationSpeed;
			}
		}

		if (!bRotatedByOtherObject)
		{
			rotation += gameContext.deltaTime * pRotationSpeed;
		}

		real overshoot = 0.0f;
		if (rotation > maxRotation)
		{
			overshoot = rotation - maxRotation;
			rotation = maxRotation;
		}
		else if (rotation < minRotation)
		{
			overshoot = minRotation - rotation;
			rotation = minRotation;
		}

		pRotation = rotation;

		if (overshoot != 0.0f &&
			currentAbsAvgRotationSpeed > 0.01f)
		{
			real gain = glm::abs(overshoot) * 8.0f;
			gain = glm::clamp(gain, 0.0f, 1.0f);
			AudioManager::SetSourceGain(s_BunkSound, gain);
			AudioManager::PlaySource(s_BunkSound, true);
			//Logger::LogInfo(std::to_string(overshoot) + ", " + std::to_string(gain));
			rotationSpeed = 0.0f;
			pRotationSpeed = 0.0f;
		}

		m_RigidBody->GetRigidBodyInternal()->activate(true);
		m_Transform.SetLocalRotation(glm::quat(glm::vec3(0, rotation, 0)));
		m_RigidBody->UpdateParentTransform();

		if (glm::abs(rotationSpeed) > 0.2f)
		{
			bool updateGain = !s_SqueakySounds.IsPlaying();

			s_SqueakySounds.Play(false);

			if (updateGain)
			{
				s_SqueakySounds.SetGain(glm::abs(rotationSpeed) * 2.0f - 0.2f);
			}
		}
	}

	RisingBlock::RisingBlock(const std::string& name) :
		GameObject(name, GameObjectType::RISING_BLOCK)
	{
	}

	GameObject* RisingBlock::CopySelf(const GameContext& gameContext, GameObject* parent, const std::string& newObjectName, bool bCopyChildren)
	{
		RisingBlock* newGameObject = new RisingBlock(newObjectName);

		newGameObject->valve = valve;
		newGameObject->moveAxis = moveAxis;
		newGameObject->bAffectedByGravity = bAffectedByGravity;
		newGameObject->startingPos = startingPos;

		CopyGenericFields(gameContext, newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void RisingBlock::ParseUniqueFields(const GameContext& gameContext, const JSONObject& parentObj, BaseScene* scene, MaterialID matID)
	{
		if (!m_MeshComponent)
		{
			VertexAttributes requiredVertexAttributes = 0;
			if (matID != InvalidMaterialID)
			{
				Material& material = gameContext.renderer->GetMaterial(matID);
				Shader& shader = gameContext.renderer->GetShader(material.shaderID);
				requiredVertexAttributes = shader.vertexAttributes;
			}

			MeshComponent* cubeMesh = new MeshComponent(matID, this);
			cubeMesh->SetRequiredAttributes(requiredVertexAttributes);
			cubeMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/cube.gltf");
			SetMeshComponent(cubeMesh);
		}

		if (!m_RigidBody)
		{
			RigidBody* rigidBody = SetRigidBody(new RigidBody());
			rigidBody->SetMass(1.0f);
			rigidBody->SetKinematic(true);
			rigidBody->SetStatic(false);
		}

		std::string valveName;

		JSONObject blockInfo;
		if (parentObj.SetObjectChecked("block info", blockInfo))
		{
			valveName = blockInfo.GetString("valve name");
		}

		if (valveName.empty())
		{
			Logger::LogWarning("Rising block's \"valve name\" field is empty! Can't find matching valve");
		}
		else
		{
			for (GameObject* rootObject : scene->GetRootObjects())
			{
				if (rootObject->GetName().compare(valveName) == 0)
				{
					valve = (Valve*)rootObject;
					break;
				}
			}
		}

		if (!valve)
		{
			Logger::LogError("Rising block contains invalid valve name! Has it been created yet? " + valveName);
		}

		blockInfo.SetBoolChecked("affected by gravity", bAffectedByGravity);

		blockInfo.SetVec3Checked("move axis", moveAxis);
		if (moveAxis == glm::vec3(0.0f))
		{
			Logger::LogWarning("Rising block's move axis is not set! It won't be able to move");
		}
	}

	void RisingBlock::SerializeUniqueFields(JSONObject& parentObject)
	{
		JSONObject blockInfo = {};

		blockInfo.fields.push_back(JSONField("valve name", JSONValue(valve->GetName())));
		blockInfo.fields.push_back(JSONField("move axis", JSONValue(Vec3ToString(moveAxis))));
		blockInfo.fields.push_back(JSONField("affected by gravity", JSONValue(bAffectedByGravity)));

		parentObject.fields.push_back(JSONField("block info", JSONValue(blockInfo)));
	}

	void RisingBlock::Initialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);

		startingPos = m_Transform.GetWorldPosition();
	}

	void RisingBlock::PostInitialize(const GameContext& gameContext)
	{
		UNREFERENCED_PARAMETER(gameContext);

		auto rbInternal = m_RigidBody->GetRigidBodyInternal();
		rbInternal->setGravity(btVector3(0, 0, 0));

		//btTransform transform = m_RigidBody->GetRigidBodyInternal()->getWorldTransform();
		//btFixedConstraint* constraint = new btFixedConstraint(
		//	*m_RigidBody->GetRigidBodyInternal(),
		//	*m_RigidBody->GetRigidBodyInternal(),
		//	transform,
		//	transform);
		////constraint->setAngularLowerLimit(btVector3(0, 0, 0));
		////constraint->setAngularUpperLimit(btVector3(0, 0, 0));
		//m_RigidBody->AddConstraint(constraint);
	}

	void RisingBlock::Update(const GameContext& gameContext)
	{
		real minDist = valve->minRotation;
		real maxDist = valve->maxRotation;
		//real totalDist = (maxDist - minDist);
		real dist = valve->rotation;

		real playerControlledValveRotationSpeed = 0.0f;
		if (valve->GetObjectInteractingWith())
		{
			i32 playerIndex = ((Player*)valve->GetObjectInteractingWith())->GetIndex();
			const InputManager::GamepadState& gamepadState = gameContext.inputManager->GetGamepadState(playerIndex);
			playerControlledValveRotationSpeed = (-gamepadState.averageRotationSpeeds.currentAverage) *
				valve->rotationSpeedScale;
		}

		if (bAffectedByGravity &&
			valve->rotation >=
			valve->minRotation + 0.1f)
		{
			// Apply gravity by rotating valve
			real fallSpeed = 6.0f;
			real distMult = 1.0f - glm::clamp(playerControlledValveRotationSpeed / 2.0f, 0.0f, 1.0f);
			real dDist = (fallSpeed * gameContext.deltaTime * distMult);
			dist -= Lerp(pdDistBlockMoved, dDist, 0.1f);
			pdDistBlockMoved = dDist;

			// NOTE: Don't clamp out of bounds rotation here, valve object 
			// will handle it and play correct "overshoot" sound
			//dist = glm::clamp(dist, minDist, maxDist);

			valve->rotation = dist;
		}

		glm::vec3 newPos = startingPos +
			dist * moveAxis;

		if (m_RigidBody)
		{
			m_RigidBody->GetRigidBodyInternal()->activate(true);
			btTransform transform;
			m_RigidBody->GetRigidBodyInternal()->getMotionState()->getWorldTransform(transform);
			transform.setOrigin(Vec3ToBtVec3(newPos));
			transform.setRotation(btQuaternion::getIdentity());
			m_RigidBody->GetRigidBodyInternal()->setWorldTransform(transform);
		}

		btVector3 startPos = Vec3ToBtVec3(startingPos);
		gameContext.renderer->GetDebugDrawer()->drawLine(
			startPos,
			startPos + Vec3ToBtVec3(moveAxis * maxDist),
			btVector3(1, 1, 1));
		if (minDist < 0.0f)
		{
			gameContext.renderer->GetDebugDrawer()->drawLine(
				startPos,
				startPos + Vec3ToBtVec3(moveAxis * minDist),
				btVector3(0.99f, 0.6f, 0.6f));
		}

		gameContext.renderer->GetDebugDrawer()->drawLine(
			startPos,
			startPos + Vec3ToBtVec3(moveAxis * dist),
			btVector3(0.3f, 0.3f, 0.5f));
	}

	ReflectionProbe::ReflectionProbe(const std::string& name) :
		GameObject(name, GameObjectType::REFLECTION_PROBE)
	{
	}

	GlassPane::GlassPane(const std::string& name) :
		GameObject(name, GameObjectType::GLASS_PANE)
	{
	}

	GameObject* GlassPane::CopySelf(const GameContext& gameContext, GameObject* parent, const std::string& newObjectName, bool bCopyChildren)
	{
		GlassPane* newGameObject = new GlassPane(newObjectName);

		newGameObject->bBroken = bBroken;

		CopyGenericFields(gameContext, newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void GlassPane::ParseUniqueFields(const GameContext& gameContext, const JSONObject& parentObj, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);

		JSONObject glassInfo;
		if (parentObj.SetObjectChecked("window info", glassInfo))
		{
			glassInfo.SetBoolChecked("broken", bBroken);

			if (!m_MeshComponent)
			{
				VertexAttributes requiredVertexAttributes = 0;
				if (matID != InvalidMaterialID)
				{
					Material& material = gameContext.renderer->GetMaterial(matID);
					Shader& shader = gameContext.renderer->GetShader(material.shaderID);
					requiredVertexAttributes = shader.vertexAttributes;
				}

				MeshComponent* windowMesh = new MeshComponent(matID, this);
				windowMesh->SetRequiredAttributes(requiredVertexAttributes);
				windowMesh->LoadFromFile(gameContext, RESOURCE_LOCATION +
					(bBroken ? "models/glass-window-broken.gltf" : "models/glass-window-whole.gltf"));
				SetMeshComponent(windowMesh);
			}
		}

		if (!m_RigidBody)
		{
			RigidBody* rigidBody = SetRigidBody(new RigidBody());
			rigidBody->SetMass(1.0f);
			rigidBody->SetKinematic(true);
			rigidBody->SetStatic(false);
		}
	}

	void GlassPane::SerializeUniqueFields(JSONObject& parentObject)
	{
		JSONObject windowInfo = {};

		windowInfo.fields.push_back(JSONField("broken", JSONValue(bBroken)));

		parentObject.fields.push_back(JSONField("window info", JSONValue(windowInfo)));
	}

	GameObject* ReflectionProbe::CopySelf(const GameContext& gameContext, GameObject* parent, const std::string& newObjectName, bool bCopyChildren)
	{
		ReflectionProbe* newGameObject = new ReflectionProbe(newObjectName);

		newGameObject->captureMatID = captureMatID;

		CopyGenericFields(gameContext, newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void ReflectionProbe::ParseUniqueFields(const GameContext& gameContext, const JSONObject& parentObj, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);
		UNREFERENCED_PARAMETER(parentObj);

		Material& material = gameContext.renderer->GetMaterial(matID);
		Shader& shader = gameContext.renderer->GetShader(material.shaderID);
		VertexAttributes requiredVertexAttributes = shader.vertexAttributes;

		// Probe capture material
		MaterialCreateInfo probeCaptureMatCreateInfo = {};
		probeCaptureMatCreateInfo.name = "Reflection probe capture";
		probeCaptureMatCreateInfo.shaderName = "deferred_combine_cubemap";
		probeCaptureMatCreateInfo.generateReflectionProbeMaps = true;
		probeCaptureMatCreateInfo.generateHDRCubemapSampler = true;
		probeCaptureMatCreateInfo.generatedCubemapSize = glm::vec2(512.0f, 512.0f); // TODO: Add support for non-512.0f size
		probeCaptureMatCreateInfo.generateCubemapDepthBuffers = true;
		probeCaptureMatCreateInfo.enableIrradianceSampler = true;
		probeCaptureMatCreateInfo.generateIrradianceSampler = true;
		probeCaptureMatCreateInfo.generatedIrradianceCubemapSize = { 32, 32 };
		probeCaptureMatCreateInfo.enablePrefilteredMap = true;
		probeCaptureMatCreateInfo.generatePrefilteredMap = true;
		probeCaptureMatCreateInfo.generatedPrefilteredCubemapSize = { 128, 128 };
		probeCaptureMatCreateInfo.enableBRDFLUT = true;
		probeCaptureMatCreateInfo.frameBuffers = {
			{ "positionMetallicFrameBufferSampler", nullptr },
			{ "normalRoughnessFrameBufferSampler", nullptr },
			{ "albedoAOFrameBufferSampler", nullptr },
		};
		captureMatID = gameContext.renderer->InitializeMaterial(&probeCaptureMatCreateInfo);

		MeshComponent* sphereMesh = new MeshComponent(matID, this);
		sphereMesh->SetRequiredAttributes(requiredVertexAttributes);

		assert(m_MeshComponent == nullptr);
		sphereMesh->LoadFromFile(gameContext, RESOURCE_LOCATION + "models/ico-sphere.gltf");
		SetMeshComponent(sphereMesh);

		std::string captureName = m_Name + "_capture";
		GameObject* captureObject = new GameObject(captureName, GameObjectType::NONE);
		captureObject->SetSerializable(false);
		captureObject->SetVisible(false);
		captureObject->SetVisibleInSceneExplorer(false);

		RenderObjectCreateInfo captureObjectCreateInfo = {};
		captureObjectCreateInfo.vertexBufferData = nullptr;
		captureObjectCreateInfo.materialID = captureMatID;
		captureObjectCreateInfo.gameObject = captureObject;
		captureObjectCreateInfo.visibleInSceneExplorer = false;

		RenderID captureRenderID = gameContext.renderer->InitializeRenderObject(&captureObjectCreateInfo);
		captureObject->SetRenderID(captureRenderID);

		AddChild(captureObject);

		gameContext.renderer->SetReflectionProbeMaterial(captureMatID);
	}

	void ReflectionProbe::SerializeUniqueFields(JSONObject& parentObject)
	{
		UNREFERENCED_PARAMETER(parentObject);

		// Reflection probes have no unique fields currently
	}

	void ReflectionProbe::PostInitialize(const GameContext& gameContext)
	{
		gameContext.renderer->SetReflectionProbeMaterial(captureMatID);
	}

	Skybox::Skybox(const std::string& name) :
		GameObject(name, GameObjectType::SKYBOX)
	{
	}

	GameObject* Skybox::CopySelf(const GameContext& gameContext, GameObject* parent, const std::string& newObjectName, bool bCopyChildren)
	{
		Skybox* newGameObject = new Skybox(newObjectName);

		CopyGenericFields(gameContext, newGameObject, parent, bCopyChildren);

		return newGameObject;
	}

	void Skybox::ParseUniqueFields(const GameContext& gameContext, const JSONObject& parentObj, BaseScene* scene, MaterialID matID)
	{
		UNREFERENCED_PARAMETER(scene);

		Material& material = gameContext.renderer->GetMaterial(matID);
		Shader& shader = gameContext.renderer->GetShader(material.shaderID);
		VertexAttributes requiredVertexAttributes = shader.vertexAttributes;

		assert(m_MeshComponent == nullptr);
		MeshComponent* skyboxMesh = new MeshComponent(matID, this);
		skyboxMesh->SetRequiredAttributes(requiredVertexAttributes);
		skyboxMesh->LoadPrefabShape(gameContext, MeshComponent::PrefabShape::SKYBOX);
		SetMeshComponent(skyboxMesh);

		JSONObject skyboxInfo;
		if (parentObj.SetObjectChecked("skybox info", skyboxInfo))
		{
			glm::vec3 rotEuler;
			if (skyboxInfo.SetVec3Checked("rotation", rotEuler))
			{
				m_Transform.SetWorldRotation(glm::quat(rotEuler));
			}
		}

		gameContext.renderer->SetSkyboxMesh(this);
	}

	void Skybox::SerializeUniqueFields(JSONObject& parentObject)
	{
		UNREFERENCED_PARAMETER(parentObject);

		JSONObject skyboxInfo = {};
		std::string eulerRotStr = Vec3ToString(glm::eulerAngles(m_Transform.GetWorldRotation()));
		skyboxInfo.fields.push_back(JSONField("rotation", JSONValue(eulerRotStr)));

		parentObject.fields.push_back(JSONField("skybox info", JSONValue(skyboxInfo)));
	}

} // namespace flex
