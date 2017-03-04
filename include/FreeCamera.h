#pragma once

#include <glm\mat4x4.hpp>
#include <glm\vec3.hpp>
#include <glm\gtc\quaternion.hpp>

struct GameContext;

class FreeCamera final
{
public:
	FreeCamera(const GameContext& gameContext, float FOV = 45.0f, float zNear = 0.1f, float zFar = 1000.0f);
	~FreeCamera();

	void Update(const GameContext& gameContext);

	void SetFOV(float FOV);
	void SetZNear(float zNear);
	void SetZFar(float zFar);
	void SetClearColor(glm::vec3 clearColor);
	glm::mat4 GetViewProjection() const;

	void SetMoveSpeed(float moveSpeed);
	void SetRotationSpeed(float rotationSpeed);

	void Translate(glm::vec3 translation);
	void SetPosition(glm::vec3 position);

	void ResetPosition();
	void ResetOrientation();

private:
	void RecalculateViewProjection(const GameContext& gameContext);

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
	
	glm::vec3 m_ClearColor;

	float m_MoveSpeed;
	float m_MoveSpeedMultiplier;
	float m_RotationSpeed;

};
