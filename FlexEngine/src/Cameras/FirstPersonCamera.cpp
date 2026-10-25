#include "stdafx.hpp"

#include "Cameras/FirstPersonCamera.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp> // For rotate
#include <glm/vec2.hpp>
IGNORE_WARNINGS_POP

#include "Cameras/CameraManager.hpp"
#include "Helpers.hpp"
#include "Player.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/SceneManager.hpp"
#include "Traversal.hpp"
#include "Window/Window.hpp"

namespace flex
{
	FirstPersonCamera::FirstPersonCamera(real FOV) :
		BaseCamera("first-person", CameraType::FIRST_PERSON, true, FOV)
	{
		bIsFirstPerson = true;
		bPossessPlayer = true;
		ResetOrientation();
		RecalculateViewProjection();
	}

	FirstPersonCamera::~FirstPersonCamera()
	{
	}

	void FirstPersonCamera::Initialize()
	{
		if (!m_bInitialized)
		{
			FindPlayer();
			Update();

			BaseCamera::Initialize();
		}
	}

	void FirstPersonCamera::OnPostSceneChange()
	{
		BaseCamera::OnPostSceneChange();

		FindPlayer();
		TrackPlayer();

		if (m_Player != nullptr)
		{
			m_Player->UpdateIsPossessed();
		}
	}

	void FirstPersonCamera::FixedUpdate()
	{
		TrackPlayer();
	}

	void FirstPersonCamera::LateUpdate()
	{
		TrackPlayer();
		BaseCamera::LateUpdate();
	}

	void FirstPersonCamera::TrackPlayer()
	{
		if (m_Player == nullptr)
		{
			return;
		}

		Transform* playerTransform = m_Player->GetTransform();

		forward = m_Player->GetLookDirection();
		right = playerTransform->GetRight();
		up = cross(forward, right);

		//btTransform t;
		//m_Player->GetRigidBody()->GetRigidBodyInternal()->getMotionState()->getWorldTransform(t);
		//position = ToVec3(t.getOrigin());

		position = playerTransform->GetWorldPosition();

		CalculateYawAndPitchFromForward();

		// Movement abilities widen the view at speed & bank into turns
		Traversal* traversal = m_Player->GetTraversal();
		fovOffset = traversal != nullptr ? traversal->GetFOVOffset() : 0.0f;
		const real bank = traversal != nullptr ? traversal->GetCameraRoll() : 0.0f;
		const glm::vec3 worldUp = glm::rotate(glm::angleAxis(bank, forward), VEC3_UP);

		right = normalize(glm::cross(worldUp, forward));
		up = cross(forward, right);

		RecalculateViewProjection();
	}

	void FirstPersonCamera::FindPlayer()
	{
		BaseScene* scene = g_SceneManager->CurrentScene();
		m_Player = scene->GetPlayer(0);

		// Scenes which don't spawn a player get one at the camera's position when switching to first person. Every
		// camera is notified of scene changes, so inactive ones mustn't spawn one (at their stale position).
		if (m_Player == nullptr && scene->IsInitialized() && g_CameraManager->CurrentCamera() == this)
		{
			m_Player = scene->SpawnPlayer(position, forward);
			m_Player->UpdateIsPossessed();
		}
	}

} // namespace flex
