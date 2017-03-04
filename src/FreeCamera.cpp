
#include "FreeCamera.h"
#include "GameContext.h"
#include "InputManager.h"
#include "Logger.h"

#include <glm\vec2.hpp>
#include <glm\gtc\matrix_transform.hpp>
#include <glm\gtx\euler_angles.hpp>

#include <GLFW\glfw3.h>

using namespace glm;

FreeCamera::FreeCamera(const GameContext& gameContext, float FOV, float zNear, float zFar) :
	m_FOV(FOV), m_ZNear(zNear), m_ZFar(zFar),
	m_Position(vec3(0.0f)),
	m_MoveSpeed(10.0f),
	m_MoveSpeedMultiplier(3.5f),
	m_RotationSpeed(3.0f),
	m_Yaw(0.0f),
	m_Pitch(0.0f)
{
	RecalculateViewProjection(gameContext);
}

FreeCamera::~FreeCamera()
{
}

void FreeCamera::Update(const GameContext& gameContext)
{
	vec2 look = vec2(0.0f);
	if (gameContext.inputManager->GetMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT))
	{
		look = gameContext.inputManager->GetMouseMovement();
		look.y = -look.y;
	}

	m_Yaw += look.x * m_RotationSpeed * gameContext.deltaTime;
	m_Pitch += look.y * m_RotationSpeed * gameContext.deltaTime;

	float pitchLimit = glm::half_pi<float>() - 0.017f;
	if (m_Pitch > pitchLimit) m_Pitch = pitchLimit;
	if (m_Pitch < -pitchLimit) m_Pitch = -pitchLimit;

	m_Forward = {};
	m_Forward.x = cos(m_Pitch) * cos(m_Yaw);
	m_Forward.y = sin(m_Pitch);
	m_Forward.z = cos(m_Pitch) * sin(m_Yaw);
	m_Forward = normalize(m_Forward);

	vec3 worldUp = vec3(0.0f, 1.0f, 0.0f);

	m_Right = normalize(cross(worldUp, m_Forward));
	m_Up = cross(m_Forward, m_Right);

	vec3 translation = {};
	if (gameContext.inputManager->GetKeyDown(GLFW_KEY_W))
	{
		translation += m_Forward;
	}
	if (gameContext.inputManager->GetKeyDown(GLFW_KEY_S))
	{
		translation -= m_Forward;
	}
	if (gameContext.inputManager->GetKeyDown(GLFW_KEY_A))
	{
		translation += m_Right;
	}
	if (gameContext.inputManager->GetKeyDown(GLFW_KEY_D))
	{
		translation -= m_Right;
	}
	if (gameContext.inputManager->GetKeyDown(GLFW_KEY_E))
	{
		translation += m_Up;
	}
	if (gameContext.inputManager->GetKeyDown(GLFW_KEY_Q))
	{
		translation -= m_Up;
	}

	float speedMultiplier = 1.0f;
	if (gameContext.inputManager->GetKeyDown(GLFW_KEY_LEFT_SHIFT))
	{
		speedMultiplier = m_MoveSpeedMultiplier;
	}

	Translate(translation * m_MoveSpeed * speedMultiplier * gameContext.deltaTime);

	RecalculateViewProjection(gameContext);
}

void FreeCamera::SetFOV(float FOV)
{
	m_FOV = FOV;
}

void FreeCamera::SetZNear(float zNear)
{
	m_ZNear = zNear;
}

void FreeCamera::SetZFar(float zFar)
{
	m_ZFar = zFar;
}

void FreeCamera::SetClearColor(glm::vec3 clearColor)
{
	m_ClearColor = clearColor;
	glClearColor(m_ClearColor.x, m_ClearColor.g, m_ClearColor.b, 1.0f);
}

glm::mat4 FreeCamera::GetViewProjection() const
{
	return m_ViewProjection;
}

void FreeCamera::SetMoveSpeed(float moveSpeed)
{
	m_MoveSpeed = moveSpeed;
}

void FreeCamera::SetRotationSpeed(float rotationSpeed)
{
	m_RotationSpeed = rotationSpeed;
}

void FreeCamera::Translate(glm::vec3 translation)
{
	m_Position += translation;
}

void FreeCamera::SetPosition(glm::vec3 position)
{
	m_Position = position;
}

void FreeCamera::ResetPosition()
{
	m_Position = vec3(0.0f);
}

void FreeCamera::ResetOrientation()
{
	m_Forward = vec3(0.0f, 0.0f, 1.0f);
	m_Up = vec3(0.0f, 1.0f, 0.0f);
	m_Right = vec3(1.0f, 0.0f, 0.0f);
}

// TODO: Measure impact of calling this every frame (optimize? Only call when values change? Only update changed values)
void FreeCamera::RecalculateViewProjection(const GameContext& gameContext)
{
	const vec2 windowSize = gameContext.windowSize;
	float aspectRatio = windowSize.x / (float)windowSize.y;
	mat4 projection = perspective(m_FOV, aspectRatio, m_ZNear, m_ZFar);

	mat4 translation = translate(mat4(1.0f), m_Position);

	mat4 view = lookAt(m_Position, m_Position + m_Forward, m_Up);
	m_ViewProjection = projection * view;
}
