#include "stdafx.hpp"

#pragma warning(push, 0)
#include <LinearMath/btTransform.h>
#include <BulletCollision/BroadphaseCollision/btBroadphaseProxy.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#pragma warning(pop)

#include "Scene/GameObject.hpp"
#include "Helpers.hpp"
#include "Physics/RigidBody.hpp"
#include "Scene/MeshComponent.hpp"

namespace flex
{
	GameObject::GameObject(const std::string& name, SerializableType serializableType) :
		m_Name(name),
		m_SerializableType(serializableType)
	{
		m_Transform.SetAsIdentity();
		m_Transform.SetGameObject(this);
	}

	GameObject::~GameObject()
	{
	}

	void GameObject::Initialize(const GameContext& gameContext)
	{
		if (m_RigidBody && m_CollisionShape)
		{
			btTransform startingTransform = TransformToBtTransform(m_Transform);
			m_RigidBody->Initialize(m_CollisionShape, gameContext, startingTransform);
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Initialize(gameContext);
		}
	}

	void GameObject::PostInitialize(const GameContext& gameContext)
	{
		if (m_RenderID != InvalidRenderID)
		{
			gameContext.renderer->PostInitializeRenderObject(gameContext, m_RenderID);
		}

		for (auto child : m_Children)
		{
			child->PostInitialize(gameContext);
		}
	}

	void GameObject::Destroy(const GameContext& gameContext)
	{
		if (m_RenderID != InvalidRenderID)
		{
			gameContext.renderer->DestroyRenderObject(m_RenderID);
		}

		if (m_MeshComponent)
		{
			m_MeshComponent->Destroy(gameContext);
			SafeDelete(m_MeshComponent);
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Destroy(gameContext);
			SafeDelete(*iter);
		}
		m_Children.clear();

		// NOTE: SafeDelete does not work on this type
		if (m_CollisionShape)
		{
			delete m_CollisionShape;
			m_CollisionShape = nullptr;
		}
		SafeDelete(m_RigidBody);
	}

	void GameObject::Update(const GameContext& gameContext)
	{
		switch (m_SerializableType)
		{
		case SerializableType::OBJECT:
		{
		} break;
		case SerializableType::SKYBOX:
		{
		} break;
		case SerializableType::REFLECTION_PROBE:
		{
		} break;
		case SerializableType::VALVE:
		{
			static RollingAverage averageRotationSpeeds(8);

			//  0 | 1 
			// - -1 --
			//  3 | 2
			static i32 stickStartingQuadrant = -1;
			static real stickStartTime = -1.0f;
			static bool wasInQuadrantSinceIdle[4];

			static real rotation = 0.0f;

			static real pJoystickX = 0.0f;
			static real pJoystickY = 0.0f;

			real joystickX = gameContext.inputManager->GetGamepadAxisValue(0, InputManager::GamepadAxis::RIGHT_STICK_X);
			real joystickY = gameContext.inputManager->GetGamepadAxisValue(0, InputManager::GamepadAxis::RIGHT_STICK_Y);

			//Logger::LogInfo(std::to_string(pJoystickX) + " " + std::to_string(pJoystickY) + "\n" +
			//				std::to_string(joystickX) + " " + std::to_string(joystickY));

			static real stickRotationSpeed = 0.0f;
			static real pStickRotationSpeed = 0.0f;

			static bool rotatingCW = false;

			static i32 previousQuadrant = -1;
			i32 currentQuadrant = -1;

			real minimumExtensionLength = 0.35f;
			real extensionLength = glm::length(glm::vec2(joystickX, joystickY));
			if (extensionLength > minimumExtensionLength)
			{
				if (stickStartTime == -1.0f)
				{
					stickStartTime = gameContext.elapsedTime;
				}

				//if (pJoystickX < 0.0f && joystickX >= 0.0f)
				//{
				//	rotatingCW = (joystickY < 0.0f);
				//}
				//else if (pJoystickX >= 0.0f && joystickX < 0.0f)
				//{
				//	rotatingCW = (joystickY >= 0.0f);
				//}

				//if (pJoystickY < 0.0f && joystickY >= 0.0f)
				//{
				//	rotatingCW = (joystickX >= 0.0f);
				//}
				//else if (pJoystickY >= 0.0f && joystickY < 0.0f)
				//{
				//	rotatingCW = (joystickX < 0.0f);
				//}

				// Ignore previous values generated when stick was inside dead zone
				if (previousQuadrant != -1)
				{
					real currentAngle = atan2(joystickY, joystickX) + PI;
					real previousAngle = atan2(pJoystickY, pJoystickX) + PI;
					// Asymptote occurs on left
					if (joystickX < 0.0f)
					{
						if (pJoystickY < 0.0f && joystickY >= 0.0f)
						{
							// CCW
							currentAngle -= TWO_PI;
						}
						else if (pJoystickY >= 0.0f && joystickY < 0.0f)
						{
							// CW
							currentAngle += TWO_PI;
						}
					}
					real currentRotationSpeed = (currentAngle - previousAngle) / gameContext.deltaTime;

					averageRotationSpeeds.AddValue(currentRotationSpeed);
					stickRotationSpeed = (-averageRotationSpeeds.currentAverage) * 0.45f;
					real maxStickSpeed = 6.0f;
					stickRotationSpeed = glm::clamp(stickRotationSpeed, -maxStickSpeed, maxStickSpeed);

					//Logger::LogInfo(std::to_string(currentAngle) + ", " + 
					//				std::to_string(previousAngle) + 
					//				" diff: " + std::to_string(currentAngle - previousAngle) + 
					//				" cur: " + std::to_string(currentRotationSpeed) + 
					//				" avg: " + std::to_string(stickRotationSpeed));
				}

				if (joystickX > 0.0f)
				{
					currentQuadrant = (joystickY > 0.0f ? 2 : 1);
				}
				else
				{
					currentQuadrant = (joystickY > 0.0f ? 3 : 0);
				}
				wasInQuadrantSinceIdle[currentQuadrant] = true;
			}
			else
			{
				stickStartingQuadrant = -1;
				stickStartTime = -1.0f;
				stickRotationSpeed = 0.0f;
				averageRotationSpeeds.Reset();
				for (i32 i = 0; i < 4; ++i)
				{
					wasInQuadrantSinceIdle[i] = false;
				}
			}

			if (previousQuadrant != currentQuadrant)
			{
				if (previousQuadrant == -1)
				{
					stickStartingQuadrant = currentQuadrant;
				}
				else
				{
					//bool rotatingCW = (previousQuadrant - currentQuadrant < 0);
					//if (previousQuadrant == 3 && currentQuadrant == 0)
					//{
					//	rotatingCW = true;
					//}
					//else if (previousQuadrant == 0 && currentQuadrant == 3)
					//{
					//	rotatingCW = false;
					//}

					//rotatingCW = rotatingCW;

					if (stickStartingQuadrant == currentQuadrant)
					{
						bool touchedAllQuadrants = true;
						for (i32 i = 0; i < 4; ++i)
						{
							if (!wasInQuadrantSinceIdle[i])
							{
								touchedAllQuadrants = false;
							}
						}

						if (touchedAllQuadrants)
						{
							Logger::LogInfo("Full loop (" + std::string(rotatingCW ? "CW" : "CCW") + ")");

							//real rotationTime = gameContext.elapsedTime - stickStartTime;
							//stickRotationSpeed = glm::clamp(1.0f / rotationTime, 0.1f, 5.0f);

							//stickStartTime = -1.0f;

							//for (i32 i = 0; i < 4; ++i)
							//{
							//	if (i != currentQuadrant)
							//	{
							//		wasInQuadrantSinceIdle[i] = false;
							//	}
							//}
						}
					}
				}
			}

			if (stickRotationSpeed != 0.0f)
			{
				pStickRotationSpeed = stickRotationSpeed;
			}
			else
			{
				pStickRotationSpeed *= 0.8f;
			}

			previousQuadrant = currentQuadrant;

			pJoystickX = joystickX;
			pJoystickY = joystickY;

			real rotationSpeed = 2.0f * gameContext.deltaTime * pStickRotationSpeed;
			rotation += (rotatingCW ? -rotationSpeed : rotationSpeed);

			m_RigidBody->GetRigidBodyInternal()->activate(true);
			m_RigidBody->SetRotation(glm::quat(glm::vec3(0, rotation, 0)));

		} break;
		case SerializableType::NONE:
		default:
		{
		} break;
		}

		if (m_RigidBody)
		{
			m_Transform.MatchRigidBody(m_RigidBody, true);
		}

		for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
		{
			(*iter)->Update(gameContext);
		}
	}

	GameObject* GameObject::GetParent()
	{
		return m_Parent;
	}

	void GameObject::SetParent(GameObject* parent)
	{
		if (parent == this)
		{
			Logger::LogError("Attempted to set parent as self! (" + m_Name + ")");
			return;
		}

		m_Parent = parent;

		m_Transform.Update();
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

		m_Transform.Update();

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
				child->SetParent(nullptr);
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

	RenderID GameObject::GetRenderID() const
	{
		return m_RenderID;
	}

	std::string GameObject::GetName() const
	{
		return m_Name;
	}

	void GameObject::SetRenderID(RenderID renderID)
	{
		m_RenderID = renderID;
	}

	bool GameObject::IsSerializable() const
	{
		return m_bSerializable;
	}

	void GameObject::SetSerializable(bool serializable)
	{
		m_bSerializable = serializable;
	}

	bool GameObject::IsStatic() const
	{
		return m_bStatic;
	}

	void GameObject::SetStatic(bool newStatic)
	{
		m_bStatic = newStatic;
	}

	bool GameObject::IsVisible() const
	{
		return m_bVisible;
	}

	void GameObject::SetVisible(bool visible, bool effectChildren)
	{
		m_bVisible = visible;

		if (effectChildren)
		{
			for (GameObject* child : m_Children)
			{
				child->SetVisible(visible, effectChildren);
			}
		}
	}

	bool GameObject::IsVisibleInSceneExplorer() const
	{
		return m_bVisibleInSceneExplorer;
	}

	void GameObject::SetVisibleInSceneExplorer(bool visibleInSceneExplorer)
	{
		m_bVisibleInSceneExplorer = visibleInSceneExplorer;
	}

	btCollisionShape* GameObject::SetCollisionShape(btCollisionShape* collisionShape)
	{
		m_CollisionShape = collisionShape;
		return collisionShape;
	}

	btCollisionShape* GameObject::GetCollisionShape() const
	{
		return m_CollisionShape;
	}

	RigidBody* GameObject::SetRigidBody(RigidBody* rigidBody)
	{
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
	}

	void GameObject::OnOverlapEnd(GameObject* other)
	{
	}
} // namespace flex
