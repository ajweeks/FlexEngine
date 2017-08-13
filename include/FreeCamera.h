#pragma once

#include <glm\mat4x4.hpp>
#include <glm\vec3.hpp>
#include <glm\gtc\quaternion.hpp>

struct GameContext;

class FreeCamera final
{
public:
	FreeCamera(GameContext& gameContext, float FOV = 45.0f, float zNear = 0.1f, float zFar = 10000.0f);
	~FreeCamera();

	void Update(const GameContext& gameContext);

	void SetFOV(float FOV);
	void SetZNear(float zNear);
	void SetZFar(float zFar);
	glm::mat4 GetViewProjection() const;
	glm::mat4 GetView() const;
	glm::mat4 GetProjection() const;

	void SetMoveSpeed(float moveSpeed);
	void SetRotationSpeed(float rotationSpeed);

	void Translate(glm::vec3 translation);
	void SetPosition(glm::vec3 position);
	glm::vec3 GetPosition() const;

	void ResetPosition();
	void ResetOrientation();

	void LoadDefaultKeybindings();
	void LoadAzertyKeybindings();

private:
	void RecalculateViewProjection(const GameContext& gameContext);

	glm::mat4 m_View;
	glm::mat4 m_Proj;
	glm::mat4 m_ViewProjection;

	float m_FOV;
	float m_ZNear;
	float m_ZFar;

	glm::vec3 m_Position;

	float m_Yaw;
	float m_Pitch;
	glm::vec3 m_Forward;
	glm::vec3 m_Up;
	glm::vec3 m_Right;

	float m_MoveSpeed;
	float m_ZoomSpeed;
	float m_MoveSpeedFastMultiplier;
	float m_MoveSpeedSlowMultiplier;
	float m_RotationSpeed;

	InputManager::KeyCode m_MoveForwardKey;
	InputManager::KeyCode m_MoveBackwardKey;
	InputManager::KeyCode m_MoveLeftKey;
	InputManager::KeyCode m_MoveRightKey;
	InputManager::KeyCode m_MoveUpKey;
	InputManager::KeyCode m_MoveDownKey;
	InputManager::KeyCode m_MoveFasterKey;
	InputManager::KeyCode m_MoveSlowerKey;

};
