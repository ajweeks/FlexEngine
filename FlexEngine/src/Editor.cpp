#include "stdafx.hpp"

#include "Editor.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletDynamics/Dynamics/btDynamicsWorld.h>

#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <glm/gtx/intersect.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <LinearMath/btIDebugDraw.h>
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/DebugCamera.hpp"
#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/RendererTypes.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsHelpers.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/GameObject.hpp"
#include "Scene/Mesh.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	// ImGui payload identifiers
	// (Must be 12 chars or less)
	const char* Editor::MaterialPayloadCStr = "material";
	const char* Editor::MeshPayloadCStr = "mesh";
	const char* Editor::GameObjectPayloadCStr = "gameobject";

	Editor::Editor() :
		m_MouseButtonCallback(this, &Editor::OnMouseButtonEvent),
		m_MouseMovedCallback(this, &Editor::OnMouseMovedEvent),
		m_KeyEventCallback(this, &Editor::OnKeyEvent),
		m_ActionCallback(this, &Editor::OnActionEvent),
		m_StartPointOnPlane(0.0f),
		m_LatestRayPlaneIntersection(0.0f),
		m_PlaneN(0.0f),
		m_PreviousIntersectionPoint(0.0f)
	{
		SelectNone();

		m_LMBDownPos = glm::vec2i(-1);
	}

	void Editor::Initialize()
	{
		// Transform gizmo materials
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.shaderName = "colour";
			matCreateInfo.constAlbedo = VEC3_ONE;
			matCreateInfo.persistent = true;
			matCreateInfo.visibleInEditor = false;
			matCreateInfo.name = "transform x";
			m_TransformGizmoMatXID = g_Renderer->InitializeMaterial(&matCreateInfo);
			matCreateInfo.name = "transform y";
			m_TransformGizmoMatYID = g_Renderer->InitializeMaterial(&matCreateInfo);
			matCreateInfo.name = "transform z";
			m_TransformGizmoMatZID = g_Renderer->InitializeMaterial(&matCreateInfo);
			matCreateInfo.name = "transform all";
			m_TransformGizmoMatAllID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}

		g_InputManager->BindMouseButtonCallback(&m_MouseButtonCallback, 95);
		g_InputManager->BindMouseMovedCallback(&m_MouseMovedCallback, 15);
		g_InputManager->BindKeyEventCallback(&m_KeyEventCallback, 15);
		g_InputManager->BindActionCallback(&m_ActionCallback, 15);
	}

	void Editor::PostInitialize()
	{
		CreateObjects();
	}

	void Editor::Destroy()
	{
		SelectNone();

		if (m_TransformGizmo != nullptr)
		{
			m_TransformGizmo->Destroy();
			delete m_TransformGizmo;
			m_TransformGizmo = nullptr;
		}

		if (m_GridObject != nullptr)
		{
			m_GridObject->Destroy();
			delete m_GridObject;
			m_GridObject = nullptr;
		}

		if (m_TestShape)
		{
			m_TestShape->Destroy();
			delete m_TestShape;
			m_TestShape = nullptr;
		}

		g_InputManager->UnbindMouseButtonCallback(&m_MouseButtonCallback);
		g_InputManager->UnbindMouseMovedCallback(&m_MouseMovedCallback);
		g_InputManager->UnbindKeyEventCallback(&m_KeyEventCallback);
		g_InputManager->UnbindActionCallback(&m_ActionCallback);
	}

	void Editor::EarlyUpdate()
	{
		if (g_InputManager->IsMouseButtonDown(MouseButton::LEFT))
		{
			if (!m_bDraggingGizmo)
			{
				m_HoveringAxisIndex = -1;
			}
			else
			{
				if (m_HoveringAxisIndex != ALL_AXES_IDX)
				{
					Transform* gizmoTransform = m_TransformGizmo->GetTransform();
					glm::vec3 axes[] = { gizmoTransform->GetRight(), gizmoTransform->GetUp(), gizmoTransform->GetForward() };
					static real alpha = 0.8f;
					static btVector4 colours[] = { btVector4(0.8f, 0.1f, 0.1f, alpha), btVector4(0.1f, 0.8f, 0.1f, alpha), btVector4(0.2f, 0.2f, 0.9f, alpha) };
					glm::vec3 pos = gizmoTransform->GetWorldPosition();
					glm::vec3 dir = axes[m_HoveringAxisIndex];
					btVector3 p0 = ToBtVec3(pos + dir * 1000.0f);
					btVector3 p1 = ToBtVec3(pos - dir * 1000.0f);
					g_Renderer->GetDebugDrawer()->DrawLineWithAlpha(p0, p1, colours[m_HoveringAxisIndex], colours[m_HoveringAxisIndex]);
				}
			}
		}
		else
		{
			m_bDraggingGizmo = false;
			if (!g_CameraManager->CurrentCamera()->bIsGameplayCam &&
				!m_CurrentlySelectedObjects.empty())
			{
				HandleGizmoHover();
			}
		}

#if 0
		// TEMP
		for (u32 i = 0; i < 128; ++i)
		{
			real x = sin(g_SecElapsedSinceProgramStart) + i * 0.25f;
			real z = cos(g_SecElapsedSinceProgramStart + i / 10.0f);
			g_Renderer->GetDebugDrawer()->drawLine(
				btVector3(x, 0, z),
				g_Editor->GetSelectedObjects().size() > 0 ? ToBtVec3(g_Editor->GetSelectedObjectsCenter()) : btVector3(x, 10, z),
				btVector3(sin(g_SecElapsedSinceProgramStart * 5.0f) * 0.5f + 0.5f, cos(g_SecElapsedSinceProgramStart * 2.5f) * 0.5f + 0.5f, 1.0f),
				btVector3(1.0f, 0.0f, sin(g_SecElapsedSinceProgramStart * 5.0f) * 0.5f + 0.5f));
		}
#endif

		FadeOutHeadOnGizmos();

		if (!m_CurrentlySelectedObjects.empty() && !g_CameraManager->CurrentCamera()->bIsGameplayCam)
		{
			m_TransformGizmo->SetVisible(true);
			UpdateGizmoVisibility();
			CalculateSelectedObjectsCenter();
			Transform* gizmoTransform = m_TransformGizmo->GetTransform();
			gizmoTransform->SetWorldPosition(m_SelectedObjectsCenterPos, false);
			gizmoTransform->SetWorldRotation(m_SelectedObjectRotation, true);

			glm::vec3 camPos = g_CameraManager->CurrentCamera()->position;
			real scale = glm::max(glm::distance(gizmoTransform->GetWorldPosition(), camPos) / 50.0f, 0.2f);
			gizmoTransform->SetWorldScale(glm::vec3(scale));
		}
		else
		{
			m_TransformGizmo->SetVisible(false);
		}
	}

	void Editor::LateUpdate()
	{
		bool bControlDown = g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_CONTROL);
		if (bControlDown && g_InputManager->GetKeyDown(KeyCode::KEY_N))
		{
			g_SceneManager->OpenNewSceneWindow();
		}

		if (g_InputManager->GetKeyPressed(KeyCode::KEY_M))
		{
			AudioManager::ToggleMuted();
		}
	}

	void Editor::PreSceneChange()
	{
		if (m_TransformGizmo)
		{
			m_TransformGizmo->Destroy();
			delete m_TransformGizmo;
			m_TransformGizmo = nullptr;
		}

		if (m_GridObject)
		{
			m_GridObject->Destroy();
			delete m_GridObject;
			m_GridObject = nullptr;
		}

		SelectNone();
	}

	void Editor::OnSceneChanged()
	{
		CreateObjects();
	}

	bool Editor::HasSelectedObject() const
	{
		return !m_CurrentlySelectedObjects.empty();
	}

	std::vector<GameObject*> Editor::GetSelectedObjects(bool bForceIncludeChildren)
	{
		if (bForceIncludeChildren)
		{
			std::vector<GameObject*> selectedObjects;
			for (GameObject* gameObject : m_CurrentlySelectedObjects)
			{
				gameObject->AddSelfAndChildrenToVec(selectedObjects);
			}
			return selectedObjects;
		}
		else
		{
			return m_CurrentlySelectedObjects;
		}
	}

	void Editor::SetSelectedObject(GameObject* gameObject, bool bSelectChildren /* = false */)
	{
		SelectNone();

		if (gameObject != nullptr)
		{
			if (bSelectChildren)
			{
				gameObject->AddSelfAndChildrenToVec(m_CurrentlySelectedObjects);
			}
			else
			{
				m_CurrentlySelectedObjects.push_back(gameObject);
			}
		}

		CalculateSelectedObjectsCenter();
	}

	void Editor::SetSelectedObjects(const std::vector<GameObject*>& selectedObjects)
	{
		m_CurrentlySelectedObjects = selectedObjects;

		CalculateSelectedObjectsCenter();
	}

	void Editor::ToggleSelectedObject(GameObject* gameObject)
	{
		auto iter = Find(m_CurrentlySelectedObjects, gameObject);
		if (iter == m_CurrentlySelectedObjects.end())
		{
			gameObject->AddSelfAndChildrenToVec(m_CurrentlySelectedObjects);
		}
		else
		{
			m_CurrentlySelectedObjects.erase(iter);

			if (m_CurrentlySelectedObjects.empty())
			{
				SelectNone();
			}
		}

		CalculateSelectedObjectsCenter();
	}

	void Editor::AddSelectedObject(GameObject* gameObject)
	{
		auto iter = Find(m_CurrentlySelectedObjects, gameObject);
		if (iter == m_CurrentlySelectedObjects.end())
		{
			gameObject->AddSelfAndChildrenToVec(m_CurrentlySelectedObjects);

			CalculateSelectedObjectsCenter();
		}
	}

	void Editor::DeselectObject(GameObject* gameObject)
	{
		for (GameObject* selectedObj : m_CurrentlySelectedObjects)
		{
			if (selectedObj == gameObject)
			{
				gameObject->RemoveSelfAndChildrenToVec(m_CurrentlySelectedObjects);
				CalculateSelectedObjectsCenter();
				return;
			}
		}

		PrintWarn("Attempted to deselect object which wasn't selected!\n");
	}

	bool Editor::IsObjectSelected(GameObject* gameObject)
	{
		bool bSelected = (Find(m_CurrentlySelectedObjects, gameObject) != m_CurrentlySelectedObjects.end());
		return bSelected;
	}

	glm::vec3 Editor::GetSelectedObjectsCenter()
	{
		return m_SelectedObjectsCenterPos;
	}

	void Editor::SelectNone()
	{
		m_CurrentlySelectedObjects.clear();
		m_SelectedObjectsCenterPos = VEC3_ZERO;
		m_SelectedObjectDragStartPos = VEC3_ZERO;
		m_DraggingGizmoScaleLast = VEC3_ZERO;
		m_DraggingGizmoOffset = 0.0f;
		m_DraggingAxisIndex = -1;
		m_bDraggingGizmo = false;
	}

	real Editor::CalculateDeltaRotationFromGizmoDrag(
		const glm::vec3& axis,
		const glm::vec3& rayOrigin,
		const glm::vec3& rayEnd,
		glm::vec3* outIntersectionPoint)
	{
		FLEX_UNUSED(axis);

		glm::vec3 intersectionPoint(0.0f);

		Transform* gizmoTransform = m_TransformGizmo->GetTransform();
		glm::vec3 rayDir = glm::normalize(rayEnd - rayOrigin);
		glm::vec3 planeOrigin = gizmoTransform->GetWorldPosition();
		glm::vec3 cameraForward = g_CameraManager->CurrentCamera()->forward;
		glm::vec3 planeN = m_PlaneN;
		if (glm::dot(planeN, cameraForward) > 0.0f)
		{
			planeN = -planeN;
		}
		real intersectionDistance;
		if (glm::intersectRayPlane(rayOrigin, rayDir, planeOrigin, planeN, intersectionDistance))
		{
			intersectionPoint = rayOrigin + rayDir * intersectionDistance;
			if (outIntersectionPoint)
			{
				*outIntersectionPoint = intersectionPoint;
			}
			if (m_DraggingGizmoOffset == -1.0f)
			{
				m_DraggingGizmoOffset = glm::dot(intersectionPoint - m_SelectedObjectDragStartPos, m_AxisProjectedOnto);
			}

			if (m_bFirstFrameDraggingRotationGizmo)
			{
				m_StartPointOnPlane = intersectionPoint;
			}

			m_LatestRayPlaneIntersection = intersectionPoint;
		}

		glm::vec3 startVec = glm::normalize(m_StartPointOnPlane - planeOrigin);
		glm::vec3 intersectVec = glm::normalize(intersectionPoint - planeOrigin);

		glm::vec3 vecPerp = glm::cross(m_AxisOfRotation, startVec);

		// NOTE: This dot product somehow results in values > 1 occasionally, causing acos to return NaN below; clamp it
		real projectedDiff = glm::clamp(glm::dot(startVec, intersectVec), -1.0f, 1.0f);
		bool intersectVecOnSameHalfAsPerp = (glm::dot(intersectVec, vecPerp) > 0.0f);
		bool intersectVecOnSameHalfAsStartVec = (projectedDiff > 0.0f);

		// __Increment/decrement wrap count on quadrant changes__
		// The cross product above gives us a perpendicular vector to startVec
		// to project onto so we can monitor when the intersection vector
		// (from plane origin to mouse cursor on plane) crosses over 180
		// degree marks to properly negate angle returned from acos below.
		if (intersectVecOnSameHalfAsPerp && !m_bLastDotPos)
		{
			if (intersectVecOnSameHalfAsStartVec)
			{
				m_RotationGizmoWrapCount++;
			}
			else
			{
				m_RotationGizmoWrapCount--;
			}
		}
		else if (!intersectVecOnSameHalfAsPerp && m_bLastDotPos)
		{
			if (intersectVecOnSameHalfAsStartVec)
			{
				m_RotationGizmoWrapCount--;
			}
			else
			{
				m_RotationGizmoWrapCount++;
			}
		}
		// We only care if this is even or odd
		m_RotationGizmoWrapCount %= 2;

		m_bLastDotPos = intersectVecOnSameHalfAsPerp;

		real angleRaw = acos(projectedDiff);
		real angle = (m_RotationGizmoWrapCount % 2 == 0 ? angleRaw : -angleRaw);

		if (m_bFirstFrameDraggingRotationGizmo)
		{
			m_bFirstFrameDraggingRotationGizmo = false;
			m_LastAngle = angle;
		}

		real dAngle = m_LastAngle - angle;

		m_LastAngle = angle;

		return dAngle;
	}

	void Editor::UpdateGizmoVisibility()
	{
		if (m_TransformGizmo->IsVisible())
		{
			m_TranslationGizmo->SetVisible(m_CurrentTransformGizmoState == TransformState::TRANSLATE);
			m_RotationGizmo->SetVisible(m_CurrentTransformGizmoState == TransformState::ROTATE);
			m_ScaleGizmo->SetVisible(m_CurrentTransformGizmoState == TransformState::SCALE);
		}
	}

	void Editor::SetTransformState(TransformState state)
	{
		if (state != m_CurrentTransformGizmoState)
		{
			m_CurrentTransformGizmoState = state;

			UpdateGizmoVisibility();
		}
	}

	bool Editor::GetWantRenameActiveElement() const
	{
		return m_bWantRenameActiveElement;
	}

	void Editor::ClearWantRenameActiveElement()
	{
		m_bWantRenameActiveElement = false;
	}

	TransformState Editor::GetTransformState() const
	{
		return m_CurrentTransformGizmoState;
	}

	void Editor::CalculateSelectedObjectsCenter()
	{
		if (m_CurrentlySelectedObjects.empty())
		{
			return;
		}

		glm::vec3 avgPos(0.0f);
		for (GameObject* gameObject : m_CurrentlySelectedObjects)
		{
			avgPos += gameObject->GetTransform()->GetWorldPosition();
		}
		m_SelectedObjectsCenterPos = m_SelectedObjectDragStartPos;
		m_SelectedObjectRotation = m_CurrentlySelectedObjects[0]->GetTransform()->GetWorldRotation();

		m_SelectedObjectsCenterPos = (avgPos / (real)m_CurrentlySelectedObjects.size());
	}

	void Editor::SelectAll()
	{
		for (GameObject* gameObject : g_SceneManager->CurrentScene()->GetAllObjects())
		{
			m_CurrentlySelectedObjects.push_back(gameObject);
		}
		CalculateSelectedObjectsCenter();
	}

	bool Editor::IsDraggingGizmo() const
	{
		return m_bDraggingGizmo;
	}

	bool Editor::HandleGizmoHover()
	{
		if (m_bDraggingGizmo == true)
		{
			PrintError("Called HandleGizmoHover when m_bDraggingGizmo == true\n");
			return false;
		}
		if (m_DraggingAxisIndex != -1)
		{
			PrintError("Called HandleGizmoHover when m_DraggingAxisIndex == %i\n", m_DraggingAxisIndex);
			return false;
		}

		if (g_InputManager->GetMousePosition().x == -1.0f)
		{
			return false;
		}

		PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();

		btVector3 rayStart, rayEnd;
		FlexEngine::GenerateRayAtMousePos(rayStart, rayEnd);

		std::string gizmoTag = (
			m_CurrentTransformGizmoState == TransformState::TRANSLATE ? m_TranslationGizmoTag :
			(m_CurrentTransformGizmoState == TransformState::ROTATE ? m_RotationGizmoTag :
				m_ScaleGizmoTag));
		GameObject* pickedTransformGizmo = physicsWorld->PickTaggedBody(rayStart, rayEnd, gizmoTag);

		Material* xMat = g_Renderer->GetMaterial(m_TransformGizmoMatXID);
		Material* yMat = g_Renderer->GetMaterial(m_TransformGizmoMatYID);
		Material* zMat = g_Renderer->GetMaterial(m_TransformGizmoMatZID);
		Material* allMat = g_Renderer->GetMaterial(m_TransformGizmoMatAllID);
		glm::vec4 white(1.0f);

		static const real gizmoHoverMultiplier = 0.6f;
		static const glm::vec4 hoverColour(gizmoHoverMultiplier, gizmoHoverMultiplier, gizmoHoverMultiplier, 1.0f);

		if (pickedTransformGizmo)
		{
			real alphaThreshold = 0.5f; // Anything less opaque than this is not selectable
			switch (m_CurrentTransformGizmoState)
			{
			case TransformState::TRANSLATE:
			{
				std::vector<GameObject*> translationAxes = m_TranslationGizmo->GetChildren();

				if (pickedTransformGizmo == translationAxes[X_AXIS_IDX] && xMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = X_AXIS_IDX;
					xMat->colourMultiplier = hoverColour;
				}
				else if (pickedTransformGizmo == translationAxes[Y_AXIS_IDX] && yMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = Y_AXIS_IDX;
					yMat->colourMultiplier = hoverColour;
				}
				else if (pickedTransformGizmo == translationAxes[Z_AXIS_IDX] && zMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = Z_AXIS_IDX;
					zMat->colourMultiplier = hoverColour;
				}
			} break;
			case TransformState::ROTATE:
			{
				std::vector<GameObject*> rotationAxes = m_RotationGizmo->GetChildren();

				if (pickedTransformGizmo == rotationAxes[X_AXIS_IDX] && xMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = X_AXIS_IDX;
					xMat->colourMultiplier = hoverColour;
				}
				else if (pickedTransformGizmo == rotationAxes[Y_AXIS_IDX] && yMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = Y_AXIS_IDX;
					yMat->colourMultiplier = hoverColour;
				}
				else if (pickedTransformGizmo == rotationAxes[Z_AXIS_IDX] && zMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = Z_AXIS_IDX;
					zMat->colourMultiplier = hoverColour;
				}
			} break;
			case TransformState::SCALE:
			{
				std::vector<GameObject*> scaleAxes = m_ScaleGizmo->GetChildren();

				if (pickedTransformGizmo == scaleAxes[X_AXIS_IDX] && xMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = X_AXIS_IDX;
					xMat->colourMultiplier = hoverColour;
				}
				else if (pickedTransformGizmo == scaleAxes[Y_AXIS_IDX] && yMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = Y_AXIS_IDX;
					yMat->colourMultiplier = hoverColour;
				}
				else if (pickedTransformGizmo == scaleAxes[Z_AXIS_IDX] && zMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = Z_AXIS_IDX;
					zMat->colourMultiplier = hoverColour;
				}
				else if (pickedTransformGizmo == scaleAxes[ALL_AXES_IDX] && allMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = ALL_AXES_IDX;
					allMat->colourMultiplier = hoverColour;
				}
			} break;
			default:
			{
				PrintError("Unhandled transform state in Editor::HandleGizmoHover\n");
			} break;
			}
		}
		else
		{
			m_HoveringAxisIndex = -1;
		}

		if (m_DraggingAxisIndex != X_AXIS_IDX && m_HoveringAxisIndex != X_AXIS_IDX)
		{
			xMat->colourMultiplier = white;
		}
		if (m_DraggingAxisIndex != Y_AXIS_IDX && m_HoveringAxisIndex != Y_AXIS_IDX)
		{
			yMat->colourMultiplier = white;
		}
		if (m_DraggingAxisIndex != Z_AXIS_IDX && m_HoveringAxisIndex != Z_AXIS_IDX)
		{
			zMat->colourMultiplier = white;
		}
		if (m_DraggingAxisIndex != ALL_AXES_IDX && m_HoveringAxisIndex != ALL_AXES_IDX)
		{
			allMat->colourMultiplier = white;
		}

		return m_HoveringAxisIndex != -1;
	}

	void Editor::HandleGizmoClick()
	{
		assert(m_bDraggingGizmo == false);
		assert(m_HoveringAxisIndex != -1);

		m_DraggingAxisIndex = m_HoveringAxisIndex;
		m_bDraggingGizmo = true;
		m_SelectedObjectDragStartPos = m_TransformGizmo->GetTransform()->GetWorldPosition();
		m_SelectedObjectDragStartRot = m_TransformGizmo->GetTransform()->GetWorldRotation();

		real gizmoSelectedMultiplier = 0.4f;
		glm::vec4 selectedColour(gizmoSelectedMultiplier, gizmoSelectedMultiplier, gizmoSelectedMultiplier, 1.0f);

		Material* xMat = g_Renderer->GetMaterial(m_TransformGizmoMatXID);
		Material* yMat = g_Renderer->GetMaterial(m_TransformGizmoMatYID);
		Material* zMat = g_Renderer->GetMaterial(m_TransformGizmoMatZID);
		Material* allMat = g_Renderer->GetMaterial(m_TransformGizmoMatAllID);

		switch (m_CurrentTransformGizmoState)
		{
		case TransformState::TRANSLATE:
		{
			if (m_HoveringAxisIndex == X_AXIS_IDX)
			{
				xMat->colourMultiplier = selectedColour;
			}
			else if (m_HoveringAxisIndex == Y_AXIS_IDX)
			{
				yMat->colourMultiplier = selectedColour;
			}
			else if (m_HoveringAxisIndex == Z_AXIS_IDX)
			{
				zMat->colourMultiplier = selectedColour;
			}
			m_DraggingGizmoOffsetNeedsRecalculation = true;
		} break;
		case TransformState::ROTATE:
		{
			m_bFirstFrameDraggingRotationGizmo = true;

			if (m_HoveringAxisIndex == X_AXIS_IDX)
			{
				xMat->colourMultiplier = selectedColour;
			}
			else if (m_HoveringAxisIndex == Y_AXIS_IDX)
			{
				yMat->colourMultiplier = selectedColour;
			}
			else if (m_HoveringAxisIndex == Z_AXIS_IDX)
			{
				zMat->colourMultiplier = selectedColour;
			}
		} break;
		case TransformState::SCALE:
		{
			if (m_HoveringAxisIndex == X_AXIS_IDX)
			{
				xMat->colourMultiplier = selectedColour;
			}
			else if (m_HoveringAxisIndex == Y_AXIS_IDX)
			{
				yMat->colourMultiplier = selectedColour;
			}
			else if (m_HoveringAxisIndex == Z_AXIS_IDX)
			{
				zMat->colourMultiplier = selectedColour;
			}
			else if (m_HoveringAxisIndex == ALL_AXES_IDX)
			{
				allMat->colourMultiplier = selectedColour;
			}
		} break;
		default:
		{
			PrintError("Unhandled transform state in Editor::HandleGizmoClick\n");
		} break;
		}
	}

	void Editor::HandleGizmoMovement()
	{
		assert(m_HoveringAxisIndex != -1);
		assert(m_DraggingAxisIndex != -1);
		assert(m_bDraggingGizmo);
		if (m_CurrentlySelectedObjects.empty())
		{
			SelectNone();
			return;
		}

		Transform* gizmoTransform = m_TransformGizmo->GetTransform();

		const glm::vec3 gizmoUp = gizmoTransform->GetUp();
		const glm::vec3 gizmoRight = gizmoTransform->GetRight();
		const glm::vec3 gizmoForward = gizmoTransform->GetForward();

		switch (m_CurrentTransformGizmoState)
		{
		case TransformState::TRANSLATE:
		{
			if (g_InputManager->DidMouseWrap())
			{
				m_DraggingGizmoOffsetNeedsRecalculation = true;
			}
		} break;
		default:
		{
		} break;
		}

		btVector3 rayStart, rayEnd;
		FlexEngine::GenerateRayAtMousePos(rayStart, rayEnd);

		glm::vec3 rayStartG = ToVec3(rayStart);
		glm::vec3 rayEndG = ToVec3(rayEnd);
		BaseCamera* cam = g_CameraManager->CurrentCamera();
		glm::vec3 camForward = cam->forward;
		glm::vec3 camRight = cam->right;
		glm::vec3 camUp = cam->up;
		glm::vec3 planeOrigin = gizmoTransform->GetWorldPosition();
		Transform* transform = m_CurrentlySelectedObjects[0]->GetTransform();

		switch (m_CurrentTransformGizmoState)
		{
		case TransformState::TRANSLATE:
		{
			glm::vec3 dPos(0.0f);

			if (g_InputManager->DidMouseWrap())
			{
				m_DraggingGizmoOffsetNeedsRecalculation = true; // Signal to recalculate offset in CalculateRayPlaneIntersectionAlongAxis
				////m_MouseDragDistX += g_InputManager->GetMouseWrapXInPixels();
				//m_MouseDragDistY += g_InputManager->GetMouseWrapYInPixels();
				//g_InputManager->GetMouseDragDistance()
				Print("warp\n");
			}

			if (m_DraggingAxisIndex == X_AXIS_IDX)
			{
				glm::vec3 axis = gizmoRight;
				glm::vec3 planeN = gizmoForward;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoUp;
				}
				glm::vec3 p;
				glm::vec3 intersectionPont = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN, m_SelectedObjectDragStartPos, camForward, m_DraggingGizmoOffset, m_DraggingGizmoOffsetNeedsRecalculation, m_PreviousIntersectionPoint, &p);
				glm::vec3 deltaPosWS = (intersectionPont - planeOrigin);

				m_TestShape->GetTransform()->SetWorldPosition(p);

				dPos = transform->GetLocalRotation() * glm::inverse(transform->GetWorldRotation()) * deltaPosWS;
			}
			else if (m_DraggingAxisIndex == Y_AXIS_IDX)
			{
				glm::vec3 axis = gizmoUp;
				glm::vec3 planeN = gizmoRight;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoForward;
				}
				glm::vec3 p;
				glm::vec3 intersectionPont = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN, m_SelectedObjectDragStartPos, camForward, m_DraggingGizmoOffset, m_DraggingGizmoOffsetNeedsRecalculation, m_PreviousIntersectionPoint, &p);
				glm::vec3 deltaPosWS = (intersectionPont - planeOrigin);

				m_TestShape->GetTransform()->SetWorldPosition(p);

				dPos = transform->GetLocalRotation() * glm::inverse(transform->GetWorldRotation()) * deltaPosWS;
			}
			else if (m_DraggingAxisIndex == Z_AXIS_IDX)
			{
				glm::vec3 axis = gizmoForward;
				glm::vec3 planeN = gizmoUp;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoRight;
				}
				glm::vec3 p;
				glm::vec3 intersectionPont = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN, m_SelectedObjectDragStartPos, camForward, m_DraggingGizmoOffset, m_DraggingGizmoOffsetNeedsRecalculation, m_PreviousIntersectionPoint, &p);
				glm::vec3 deltaPosWS = (intersectionPont - planeOrigin);

				m_TestShape->GetTransform()->SetWorldPosition(p);

				dPos = transform->GetLocalRotation() * glm::inverse(transform->GetWorldRotation()) * deltaPosWS;
			}

			for (GameObject* gameObject : m_CurrentlySelectedObjects)
			{
				GameObject* parent = gameObject->GetParent();
				bool bObjectIsntChild = (parent == nullptr) || (Find(m_CurrentlySelectedObjects, parent) == m_CurrentlySelectedObjects.end());
				if (bObjectIsntChild)
				{
					gameObject->GetTransform()->Translate(dPos);
				}
			}
		} break;
		case TransformState::ROTATE:
		{
			glm::quat dRotWS(VEC3_ZERO);

#if 0
			glm::quat lRot = transform->GetLocalRotation();
			glm::vec3 lEuler = glm::eulerAngles(transform->GetLocalRotation());
			glm::quat wRot = transform->GetWorldRotation();
			Print("Local: %5.2f,%5.2f,%5.2f,%5.2f  -  L Euler: %5.2f,%5.2f,%5.2f - Wrap: %d\n", lRot.x, lRot.y, lRot.z, lRot.w, lEuler.x, lEuler.y, lEuler.z, m_RotationGizmoWrapCount);
			Print("Local: %5.2f,%5.2f,%5.2f,%5.2f  -  Global: %5.2f,%5.2f,%5.2f,%5.2f  -  L Euler: %5.2f,%5.2f,%5.2f\n", lRot.x, lRot.y, lRot.z, lRot.w, wRot.x, wRot.y, wRot.z, wRot.w, lEuler.x, lEuler.y, lEuler.z);
#endif

			if (m_DraggingAxisIndex == X_AXIS_IDX)
			{
				if (m_bFirstFrameDraggingRotationGizmo)
				{
					m_AxisProjectedOnto = gizmoUp;
					m_AxisOfRotation = gizmoRight;
					m_PlaneN = m_AxisOfRotation;
					if (glm::abs(glm::dot(m_AxisProjectedOnto, camForward)) > 0.5f)
					{
						m_AxisProjectedOnto = gizmoForward;
					}
				}

				glm::vec3 p;
				real dAngle = CalculateDeltaRotationFromGizmoDrag(m_AxisOfRotation, rayStartG, rayEndG, &p);
				if (dAngle != 0.0f)
				{
					dRotWS = glm::angleAxis(dAngle, m_AxisOfRotation);
				}

				m_TestShape->GetTransform()->SetWorldPosition(p);
			}
			else if (m_DraggingAxisIndex == Y_AXIS_IDX)
			{
				if (m_bFirstFrameDraggingRotationGizmo)
				{
					m_AxisProjectedOnto = gizmoRight;
					m_AxisOfRotation = gizmoUp;
					m_PlaneN = m_AxisOfRotation;
					if (glm::abs(glm::dot(m_AxisProjectedOnto, camForward)) > 0.5f)
					{
						m_AxisProjectedOnto = gizmoForward;
					}
				}

				glm::vec3 p;
				real dAngle = CalculateDeltaRotationFromGizmoDrag(m_AxisOfRotation, rayStartG, rayEndG, &p);
				if (dAngle != 0.0f)
				{
					dRotWS = glm::angleAxis(dAngle, m_AxisOfRotation);
				}

				m_TestShape->GetTransform()->SetWorldPosition(p);
			}
			else if (m_DraggingAxisIndex == Z_AXIS_IDX)
			{
				if (m_bFirstFrameDraggingRotationGizmo)
				{
					m_AxisProjectedOnto = gizmoUp;
					m_AxisOfRotation = gizmoForward;
					m_PlaneN = m_AxisOfRotation;
					if (glm::abs(glm::dot(m_AxisProjectedOnto, camForward)) > 0.5f)
					{
						m_AxisProjectedOnto = gizmoForward;
					}
				}

				glm::vec3 p;
				real dAngle = CalculateDeltaRotationFromGizmoDrag(m_AxisOfRotation, rayStartG, rayEndG, &p);
				if (dAngle != 0.0f)
				{
					dRotWS = glm::angleAxis(dAngle, m_AxisOfRotation);
				}
				m_TestShape->GetTransform()->SetWorldPosition(p);
			}

			if (dRotWS != QUAT_IDENTITY)
			{
				for (GameObject* gameObject : m_CurrentlySelectedObjects)
				{
					GameObject* parent = gameObject->GetParent();
					bool bObjectIsntChild = (parent == nullptr) || (Find(m_CurrentlySelectedObjects, parent) == m_CurrentlySelectedObjects.end());
					if (bObjectIsntChild)
					{
						Transform* t = gameObject->GetTransform();
						t->SetWorldRotation(dRotWS * t->GetWorldRotation());
					}
				}
			}
		} break;
		case TransformState::SCALE:
		{
			glm::vec3 dLocalScale(1.0f);
			bool bControlDown = g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_CONTROL);
			bool bShiftDown = g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT);
			real dragSpeed = m_ScaleDragSpeed * (bControlDown ? m_ScaleSlowDragSpeedMultiplier : bShiftDown ? m_ScaleFastDragSpeedMultiplier : 1.0f);

			if (m_DraggingAxisIndex == X_AXIS_IDX)
			{
				glm::vec3 axis = gizmoRight;
				glm::vec3 planeN = gizmoForward;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoUp;
				}
				glm::vec3 intersectionPont = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN, m_SelectedObjectDragStartPos, camForward, m_DraggingGizmoOffset, m_DraggingGizmoOffsetNeedsRecalculation, m_PreviousIntersectionPoint);
				glm::vec3 scaleNow = (intersectionPont - planeOrigin);
				glm::vec3 scaleVecGlobal = (scaleNow - m_DraggingGizmoScaleLast) * dragSpeed;
				dLocalScale += glm::vec3(glm::inverse(gizmoTransform->GetWorldTransform()) * glm::vec4(scaleVecGlobal, 0.0f));

				m_DraggingGizmoScaleLast = scaleNow;
			}
			else if (m_DraggingAxisIndex == Y_AXIS_IDX)
			{
				glm::vec3 axis = gizmoUp;
				glm::vec3 planeN = gizmoRight;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoForward;
				}
				glm::vec3 intersectionPont = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN, m_SelectedObjectDragStartPos, camForward, m_DraggingGizmoOffset, m_DraggingGizmoOffsetNeedsRecalculation, m_PreviousIntersectionPoint);
				glm::vec3 scaleNow = (intersectionPont - planeOrigin);
				glm::vec3 scaleVecGlobal = (scaleNow - m_DraggingGizmoScaleLast) * dragSpeed;
				dLocalScale += glm::vec3(glm::inverse(gizmoTransform->GetWorldTransform()) * glm::vec4(scaleVecGlobal, 0.0f));

				m_DraggingGizmoScaleLast = scaleNow;
			}
			else if (m_DraggingAxisIndex == Z_AXIS_IDX)
			{
				glm::vec3 axis = gizmoForward;
				glm::vec3 planeN = gizmoUp;
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = gizmoRight;
				}
				glm::vec3 intersectionPont = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayStartG, rayEndG, planeOrigin, planeN, m_SelectedObjectDragStartPos, camForward, m_DraggingGizmoOffset, m_DraggingGizmoOffsetNeedsRecalculation, m_PreviousIntersectionPoint);
				glm::vec3 scaleNow = (intersectionPont - planeOrigin);
				glm::vec3 scaleVecGlobal = (scaleNow - m_DraggingGizmoScaleLast) * dragSpeed;
				dLocalScale += glm::vec3(glm::inverse(gizmoTransform->GetWorldTransform()) * glm::vec4(scaleVecGlobal, 0.0f));

				m_DraggingGizmoScaleLast = scaleNow;
			}
			else if (m_DraggingAxisIndex == ALL_AXES_IDX)
			{
				glm::vec3 axis1 = -camRight;
				glm::vec3 axis2 = camUp;
				glm::vec3 planeN = -camForward;
				glm::vec3 intersectionPont1 = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis1, rayStartG, rayEndG, planeOrigin, planeN, m_SelectedObjectDragStartPos, camForward, m_DraggingGizmoOffset, m_DraggingGizmoOffsetNeedsRecalculation, m_PreviousIntersectionPoint);
				glm::vec3 intersectionPont2 = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis2, rayStartG, rayEndG, planeOrigin, planeN, m_SelectedObjectDragStartPos, camForward, m_DraggingGizmoOffset, m_DraggingGizmoOffsetNeedsRecalculation, m_PreviousIntersectionPoint);
				glm::vec3 intersectionRay1 = (intersectionPont1 - planeOrigin);
				glm::vec3 intersectionRay2 = (intersectionPont2 - planeOrigin);
				if (glm::length(intersectionRay1) > 0.0f && glm::length(intersectionRay2) > 0.0f)
				{
					real dotResult1 = glm::length(intersectionRay1) * glm::dot(intersectionRay1, axis1);
					real dotResult2 = glm::length(intersectionRay2) * glm::dot(intersectionRay2, axis2);
					real largerDot = abs(dotResult1) > abs(dotResult2) ? dotResult1 : dotResult2;
					glm::vec3 scaleNow = glm::vec3(glm::clamp(largerDot, -9999.0f, 9999.0f));
					dLocalScale += (scaleNow - m_DraggingGizmoScaleLast) * dragSpeed;

					m_DraggingGizmoScaleLast = scaleNow;
				}
			}

			dLocalScale = glm::clamp(dLocalScale, 0.01f, 10.0f);

			for (GameObject* gameObject : m_CurrentlySelectedObjects)
			{
				GameObject* parent = gameObject->GetParent();
				bool bObjectIsntChild = (parent == nullptr) || (Find(m_CurrentlySelectedObjects, parent) == m_CurrentlySelectedObjects.end());
				if (bObjectIsntChild)
				{
					gameObject->GetTransform()->Scale(dLocalScale);
				}
			}
		} break;
		default:
		{
			PrintError("Unhandled transform state in Editor::HandleGizmoMovement\n");
		} break;
		}

		m_DraggingGizmoOffsetNeedsRecalculation = false;

		CalculateSelectedObjectsCenter();
	}

	bool Editor::HandleObjectClick()
	{
		PhysicsWorld* physicsWorld = g_SceneManager->CurrentScene()->GetPhysicsWorld();

		btVector3 rayStart, rayEnd;
		FlexEngine::GenerateRayAtMousePos(rayStart, rayEnd);

		const btRigidBody* pickedRB = physicsWorld->PickFirstBody(rayStart, rayEnd);
		if (pickedRB)
		{
			GameObject* pickedObject = static_cast<GameObject*>(pickedRB->getUserPointer());

			RigidBody* rb = pickedObject->GetRigidBody();
			if (!(rb->GetPhysicsFlags() & (u32)PhysicsFlag::UNSELECTABLE))
			{
				if (g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT))
				{
					ToggleSelectedObject(pickedObject);
				}
				else
				{
					SelectNone();
					m_CurrentlySelectedObjects.push_back(pickedObject);
				}
				g_InputManager->ClearMouseInput();
				CalculateSelectedObjectsCenter();

				return true;
			}
			else
			{
				SelectNone();
			}
		}

		return false;
	}

	void Editor::OnDragDrop(i32 count, const char** paths)
	{
		std::vector<GameObject*> createdObjs;

		for (i32 i = 0; i < count; ++i)
		{
			std::string path = ReplaceBackSlashesWithForward(paths[i]);
			bool bDir = path.find('.') == std::string::npos;
			if (!bDir)
			{
				std::string fileType = ExtractFileType(path);
				if (fileType == "glb" || fileType == "gltf")
				{
					BaseScene* scene = g_SceneManager->CurrentScene();
					GameObject* newObj = new GameObject(scene->GetUniqueObjectName("New mesh ", 1), GameObjectType::OBJECT);

					std::string absoluteMeshDir = RelativePathToAbsolute(MESH_DIRECTORY);

					std::string fileName = path.substr(path.rfind('/') + 1);
					if (!StartsWith(path, absoluteMeshDir))
					{
						std::string newPath = absoluteMeshDir + fileName;
						Print("Copying mesh file from %s to %s\n", path.c_str(), newPath.c_str());
						Platform::CopyFile(path, newPath);
						path = newPath;
					}

					std::string relativeFilePath = MESH_DIRECTORY + fileName;
					Mesh::ImportFromFile(relativeFilePath, newObj);
					// TODO: Position object relative to mouse position & camera transform
					scene->AddRootObject(newObj);
					createdObjs.push_back(newObj);
				}
				else
				{
					PrintWarn("Attempted to import unhandled file type %s.\n", fileType.c_str());
				}
			}
			else
			{
				PrintWarn("Directory imports are not currently supported.\n");
			}
		}

		if (!createdObjs.empty())
		{
			SetSelectedObjects(createdObjs);
		}
	}

	bool Editor::IsShowingGrid() const
	{
		return m_bShowGrid;
	}

	void Editor::SetShowGrid(bool bShowGrid)
	{
		m_bShowGrid = bShowGrid;
		m_GridObject->SetVisible(bShowGrid);
	}

	EventReply Editor::OnMouseButtonEvent(MouseButton button, KeyAction action)
	{
		if (button == MouseButton::LEFT)
		{
			if (action == KeyAction::PRESS)
			{
				m_LMBDownPos = g_InputManager->GetMousePosition();

				if (m_HoveringAxisIndex != -1)
				{
					HandleGizmoClick();
					return EventReply::CONSUMED;
				}

				return EventReply::UNCONSUMED;
			}
			else if (action == KeyAction::RELEASE)
			{
				if (m_bDraggingGizmo)
				{
					if (m_CurrentlySelectedObjects.empty())
					{
						m_SelectedObjectDragStartPos = VEC3_NEG_ONE;
					}
					else
					{
						CalculateSelectedObjectsCenter();
						m_SelectedObjectDragStartPos = m_SelectedObjectsCenterPos;
					}

					m_bDraggingGizmo = false;
					m_DraggingAxisIndex = -1;
					m_DraggingGizmoScaleLast = VEC3_ZERO;
					m_DraggingGizmoOffset = 0.0f;
				}

				if (m_LMBDownPos != glm::vec2i(-1))
				{
					glm::vec2i releasePos = g_InputManager->GetMousePosition();
					real dPos = glm::length((glm::vec2)(releasePos - m_LMBDownPos));
					if (dPos < 1.0f)
					{
						if (HandleObjectClick())
						{
							m_LMBDownPos = glm::vec2i(-1);
							return EventReply::CONSUMED;
						}
					}

					m_LMBDownPos = glm::vec2i(-1);
				}
			}
		}

		return EventReply::UNCONSUMED;
	}

	EventReply Editor::OnMouseMovedEvent(const glm::vec2& dMousePos)
	{
		FLEX_UNUSED(dMousePos);

		if (m_bDraggingGizmo)
		{
			HandleGizmoMovement();
			return EventReply::CONSUMED;
		}
		return EventReply::UNCONSUMED;
	}

	EventReply Editor::OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers)
	{
		const bool bControlDown = (modifiers & (i32)InputModifier::CONTROL) > 0;

		if (action == KeyAction::PRESS)
		{
			if (keyCode == KeyCode::KEY_DELETE)
			{
				if (!m_CurrentlySelectedObjects.empty())
				{
					g_SceneManager->CurrentScene()->RemoveObjectsImmediate(m_CurrentlySelectedObjects, true);

					SelectNone();
					return EventReply::CONSUMED;
				}
			}

			if (bControlDown && keyCode == KeyCode::KEY_D)
			{
				if (!m_CurrentlySelectedObjects.empty())
				{
					std::vector<GameObject*> newSelectedGameObjects;

					for (GameObject* gameObject : m_CurrentlySelectedObjects)
					{
						GameObject* duplicatedObject = gameObject->CopySelfAndAddToScene();

						duplicatedObject->AddSelfAndChildrenToVec(newSelectedGameObjects);
					}

					SelectNone();

					m_CurrentlySelectedObjects = newSelectedGameObjects;
					CalculateSelectedObjectsCenter();
				}
				return EventReply::CONSUMED;
			}


			if (bControlDown && keyCode == KeyCode::KEY_A)
			{
				SelectAll();
				return EventReply::CONSUMED;
			}

			if (bControlDown && keyCode == KeyCode::KEY_S)
			{
				g_SceneManager->CurrentScene()->SerializeToFile(true);
				return EventReply::CONSUMED;
			}
		}

		return EventReply::UNCONSUMED;
	}

	EventReply Editor::OnActionEvent(Action action)
	{
		if (action == Action::EDITOR_RENAME_SELECTED)
		{
			m_bWantRenameActiveElement = !m_bWantRenameActiveElement;
			return EventReply::CONSUMED;
		}

		if (action == Action::EDITOR_SELECT_TRANSLATE_GIZMO)
		{
			SetTransformState(TransformState::TRANSLATE);
			return EventReply::CONSUMED;
		}
		if (action == Action::EDITOR_SELECT_ROTATE_GIZMO)
		{
			SetTransformState(TransformState::ROTATE);
			return EventReply::CONSUMED;
		}
		if (action == Action::EDITOR_SELECT_SCALE_GIZMO)
		{
			SetTransformState(TransformState::SCALE);
			return EventReply::CONSUMED;
		}

		if (action == Action::PAUSE)
		{
			if (m_CurrentlySelectedObjects.empty())
			{
				//m_bSimulationPaused = !m_bSimulationPaused;
			}
			else
			{
				SelectNone();
			}
			return EventReply::CONSUMED;
		}

		// TODO: Add camera types!
		if (action == Action::EDITOR_FOCUS_ON_SELECTION && !m_CurrentlySelectedObjects.empty() && dynamic_cast<DebugCamera*>(g_CameraManager->CurrentCamera()) != nullptr)
		{
			glm::vec3 minPos(FLT_MAX);
			glm::vec3 maxPos(-FLT_MAX);
			for (GameObject* gameObject : m_CurrentlySelectedObjects)
			{
				Mesh* mesh = gameObject->GetMesh();
				if (mesh)
				{
					Transform* transform = gameObject->GetTransform();
					glm::vec3 min = transform->GetWorldTransform() * glm::vec4(mesh->m_MinPoint, 1.0f);
					glm::vec3 max = transform->GetWorldTransform() * glm::vec4(mesh->m_MaxPoint, 1.0f);
					minPos = glm::min(minPos, min);
					maxPos = glm::max(maxPos, max);
				}
			}

			if (minPos.x == FLT_MAX || maxPos.x == -FLT_MAX || minPos == maxPos)
			{
				minPos = glm::vec3(-1.0f);
				maxPos = glm::vec3(1.0f);
			}

			glm::vec3 sphereCenterWS = minPos + (maxPos - minPos) / 2.0f;
			real sphereRadius = glm::length(maxPos - minPos) / 2.0f;

			BaseCamera* cam = g_CameraManager->CurrentCamera();

			if (sphereRadius > 0.0f)
			{
				glm::vec3 currentOffset = cam->position - sphereCenterWS;
				glm::vec3 newOffset;
				if (currentOffset != VEC3_ZERO)
				{
					newOffset = glm::normalize(currentOffset) * sphereRadius * 3.0f;
				}
				else
				{
					newOffset = glm::vec3(0.0f, 0.0f, sphereRadius * 3.0f);
				}
				cam->position = sphereCenterWS + newOffset;
			}

			cam->LookAt(m_SelectedObjectsCenterPos);
			return EventReply::CONSUMED;
		}
		return EventReply::UNCONSUMED;
	}

	void Editor::CreateObjects()
	{
		if (m_TransformGizmo != nullptr)
		{
			return;
		}

		RenderObjectCreateInfo gizmoCreateInfo = {};
		gizmoCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
		gizmoCreateInfo.bDepthWriteEnable = false;
		gizmoCreateInfo.bEditorObject = true;
		gizmoCreateInfo.bSetDynamicStates = true;
		gizmoCreateInfo.cullFace = CullFace::BACK;

		if (m_TestShape == nullptr)
		{
			m_TestShape = new GameObject("Test Shape", GameObjectType::OBJECT);
			Mesh* mesh = m_TestShape->SetMesh(new Mesh(m_TestShape));
			mesh->LoadFromFile(MESH_DIRECTORY "sphere.glb", m_TransformGizmoMatXID);
			m_TestShape->GetTransform()->Scale(0.5f);

			m_TestShape->SetVisible(false);
		}

		m_GridObject = new GameObject("Grid", GameObjectType::OBJECT);
		Mesh* gridMesh = m_GridObject->SetMesh(new Mesh(m_GridObject));
		RenderObjectCreateInfo gridCreateInfo = {};
		gridCreateInfo.bEditorObject = true;
		gridMesh->LoadPrefabShape(PrefabShape::GRID, m_TransformGizmoMatAllID, &gridCreateInfo);
		m_GridObject->Initialize();
		m_GridObject->PostInitialize();
		m_GridObject->SetVisible(m_bShowGrid);

		m_TransformGizmo = new GameObject("Transform gizmo", GameObjectType::_NONE);

		u32 gizmoRBFlags = ((u32)PhysicsFlag::TRIGGER) | ((u32)PhysicsFlag::UNSELECTABLE);
		i32 gizmoRBGroup = (u32)CollisionType::EDITOR_OBJECT;
		i32 gizmoRBMask = (i32)CollisionType::DEFAULT;

		// Translation gizmo
		{
			real cylinderRadius = 0.35f;
			real cylinderHeight = 1.8f;

			// X Axis
			GameObject* translateXAxis = new GameObject("Translation gizmo x axis", GameObjectType::_NONE);
			translateXAxis->AddTag(m_TranslationGizmoTag);
			Mesh* xAxisMesh = translateXAxis->SetMesh(new Mesh(translateXAxis));

			btCylinderShape* xAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			translateXAxis->SetCollisionShape(xAxisShape);

			RigidBody* gizmoXAxisRB = translateXAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoXAxisRB->SetMass(0.0f);
			gizmoXAxisRB->SetKinematic(true);
			gizmoXAxisRB->SetPhysicsFlags(gizmoRBFlags);

			xAxisMesh->LoadFromFile(MESH_DIRECTORY "translation-gizmo-x.glb", m_TransformGizmoMatXID, false, nullptr, &gizmoCreateInfo);

			// Y Axis
			GameObject* translateYAxis = new GameObject("Translation gizmo y axis", GameObjectType::_NONE);
			translateYAxis->AddTag(m_TranslationGizmoTag);
			Mesh* yAxisMesh = translateYAxis->SetMesh(new Mesh(translateYAxis));

			btCylinderShape* yAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			translateYAxis->SetCollisionShape(yAxisShape);

			RigidBody* gizmoYAxisRB = translateYAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoYAxisRB->SetMass(0.0f);
			gizmoYAxisRB->SetKinematic(true);
			gizmoYAxisRB->SetPhysicsFlags(gizmoRBFlags);

			yAxisMesh->LoadFromFile(MESH_DIRECTORY "translation-gizmo-y.glb", m_TransformGizmoMatYID, false, nullptr, &gizmoCreateInfo);

			// Z Axis
			GameObject* translateZAxis = new GameObject("Translation gizmo z axis", GameObjectType::_NONE);
			translateZAxis->AddTag(m_TranslationGizmoTag);
			Mesh* zAxisMesh = translateZAxis->SetMesh(new Mesh(translateZAxis));

			btCylinderShape* zAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			translateZAxis->SetCollisionShape(zAxisShape);

			RigidBody* gizmoZAxisRB = translateZAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoZAxisRB->SetMass(0.0f);
			gizmoZAxisRB->SetKinematic(true);
			gizmoZAxisRB->SetPhysicsFlags(gizmoRBFlags);

			zAxisMesh->LoadFromFile(MESH_DIRECTORY "translation-gizmo-z.glb", m_TransformGizmoMatZID, false, nullptr, &gizmoCreateInfo);


			gizmoXAxisRB->SetLocalRotation(glm::quat(glm::vec3(0, 0, PI / 2.0f)));
			gizmoXAxisRB->SetLocalPosition(glm::vec3(cylinderHeight, 0, 0));

			gizmoYAxisRB->SetLocalPosition(glm::vec3(0, cylinderHeight, 0));

			gizmoZAxisRB->SetLocalRotation(glm::quat(glm::vec3(PI / 2.0f, 0, 0)));
			gizmoZAxisRB->SetLocalPosition(glm::vec3(0, 0, cylinderHeight));


			m_TranslationGizmo = new GameObject("Translation gizmo", GameObjectType::_NONE);

			m_TranslationGizmo->AddChildImmediate(translateXAxis);
			m_TranslationGizmo->AddChildImmediate(translateYAxis);
			m_TranslationGizmo->AddChildImmediate(translateZAxis);

			m_TransformGizmo->AddChildImmediate(m_TranslationGizmo);
		}

		// Rotation gizmo
		{
			real cylinderRadius = 3.4f;
			real cylinderHeight = 0.2f;

			// X Axis
			GameObject* rotationXAxis = new GameObject("Rotation gizmo x axis", GameObjectType::_NONE);
			rotationXAxis->AddTag(m_RotationGizmoTag);
			Mesh* xAxisMesh = rotationXAxis->SetMesh(new Mesh(rotationXAxis));

			btCylinderShape* xAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			rotationXAxis->SetCollisionShape(xAxisShape);

			RigidBody* gizmoXAxisRB = rotationXAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoXAxisRB->SetMass(0.0f);
			gizmoXAxisRB->SetKinematic(true);
			gizmoXAxisRB->SetPhysicsFlags(gizmoRBFlags);

			xAxisMesh->LoadFromFile(MESH_DIRECTORY "rotation-gizmo-flat-x.glb", m_TransformGizmoMatXID, false, nullptr, &gizmoCreateInfo);

			// Y Axis
			GameObject* rotationYAxis = new GameObject("Rotation gizmo y axis", GameObjectType::_NONE);
			rotationYAxis->AddTag(m_RotationGizmoTag);
			Mesh* yAxisMesh = rotationYAxis->SetMesh(new Mesh(rotationYAxis));

			btCylinderShape* yAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			rotationYAxis->SetCollisionShape(yAxisShape);

			RigidBody* gizmoYAxisRB = rotationYAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoYAxisRB->SetMass(0.0f);
			gizmoYAxisRB->SetKinematic(true);
			gizmoYAxisRB->SetPhysicsFlags(gizmoRBFlags);

			yAxisMesh->LoadFromFile(MESH_DIRECTORY "rotation-gizmo-flat-y.glb", m_TransformGizmoMatYID, false, nullptr, &gizmoCreateInfo);

			// Z Axis
			GameObject* rotationZAxis = new GameObject("Rotation gizmo z axis", GameObjectType::_NONE);
			rotationZAxis->AddTag(m_RotationGizmoTag);
			Mesh* zAxisMesh = rotationZAxis->SetMesh(new Mesh(rotationZAxis));

			btCylinderShape* zAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			rotationZAxis->SetCollisionShape(zAxisShape);

			RigidBody* gizmoZAxisRB = rotationZAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoZAxisRB->SetMass(0.0f);
			gizmoZAxisRB->SetKinematic(true);
			gizmoZAxisRB->SetPhysicsFlags(gizmoRBFlags);

			zAxisMesh->LoadFromFile(MESH_DIRECTORY "rotation-gizmo-flat-z.glb", m_TransformGizmoMatZID, false, nullptr, &gizmoCreateInfo);

			gizmoXAxisRB->SetLocalRotation(glm::quat(glm::vec3(0, 0, PI / 2.0f)));
			gizmoXAxisRB->SetLocalPosition(glm::vec3(-cylinderHeight, 0, 0));

			gizmoYAxisRB->SetLocalPosition(glm::vec3(0, cylinderHeight, 0));

			gizmoZAxisRB->SetLocalRotation(glm::quat(glm::vec3(PI / 2.0f, 0, 0)));
			gizmoZAxisRB->SetLocalPosition(glm::vec3(0, 0, cylinderHeight));


			m_RotationGizmo = new GameObject("Rotation gizmo", GameObjectType::_NONE);

			m_RotationGizmo->AddChildImmediate(rotationXAxis);
			m_RotationGizmo->AddChildImmediate(rotationYAxis);
			m_RotationGizmo->AddChildImmediate(rotationZAxis);

			m_TransformGizmo->AddChildImmediate(m_RotationGizmo);

			m_RotationGizmo->SetVisible(false);
		}

		// Scale gizmo
		{
			real boxScale = 0.5f;
			real cylinderRadius = 0.35f;
			real cylinderHeight = 1.8f;

			// X Axis
			GameObject* scaleXAxis = new GameObject("Scale gizmo x axis", GameObjectType::_NONE);
			scaleXAxis->AddTag(m_ScaleGizmoTag);
			Mesh* xAxisMesh = scaleXAxis->SetMesh(new Mesh(scaleXAxis));

			btCylinderShape* xAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			scaleXAxis->SetCollisionShape(xAxisShape);

			RigidBody* gizmoXAxisRB = scaleXAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoXAxisRB->SetMass(0.0f);
			gizmoXAxisRB->SetKinematic(true);
			gizmoXAxisRB->SetPhysicsFlags(gizmoRBFlags);

			xAxisMesh->LoadFromFile(MESH_DIRECTORY "scale-gizmo-x.glb", m_TransformGizmoMatXID, false, nullptr, &gizmoCreateInfo);

			// Y Axis
			GameObject* scaleYAxis = new GameObject("Scale gizmo y axis", GameObjectType::_NONE);
			scaleYAxis->AddTag(m_ScaleGizmoTag);
			Mesh* yAxisMesh = scaleYAxis->SetMesh(new Mesh(scaleYAxis));

			btCylinderShape* yAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			scaleYAxis->SetCollisionShape(yAxisShape);

			RigidBody* gizmoYAxisRB = scaleYAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoYAxisRB->SetMass(0.0f);
			gizmoYAxisRB->SetKinematic(true);
			gizmoYAxisRB->SetPhysicsFlags(gizmoRBFlags);

			yAxisMesh->LoadFromFile(MESH_DIRECTORY "scale-gizmo-y.glb", m_TransformGizmoMatYID, false, nullptr, &gizmoCreateInfo);

			// Z Axis
			GameObject* scaleZAxis = new GameObject("Scale gizmo z axis", GameObjectType::_NONE);
			scaleZAxis->AddTag(m_ScaleGizmoTag);
			Mesh* zAxisMesh = scaleZAxis->SetMesh(new Mesh(scaleZAxis));

			btCylinderShape* zAxisShape = new btCylinderShape(btVector3(cylinderRadius, cylinderHeight, cylinderRadius));
			scaleZAxis->SetCollisionShape(zAxisShape);

			RigidBody* gizmoZAxisRB = scaleZAxis->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoZAxisRB->SetMass(0.0f);
			gizmoZAxisRB->SetKinematic(true);
			gizmoZAxisRB->SetPhysicsFlags(gizmoRBFlags);

			zAxisMesh->LoadFromFile(MESH_DIRECTORY "scale-gizmo-z.glb", m_TransformGizmoMatZID, false, nullptr, &gizmoCreateInfo);

			// Center (all axes)
			GameObject* scaleAllAxes = new GameObject("Scale gizmo all axes", GameObjectType::_NONE);
			scaleAllAxes->AddTag(m_ScaleGizmoTag);
			Mesh* allAxesMesh = scaleAllAxes->SetMesh(new Mesh(scaleAllAxes));

			btBoxShape* allAxesShape = new btBoxShape(btVector3(boxScale, boxScale, boxScale));
			scaleAllAxes->SetCollisionShape(allAxesShape);

			RigidBody* gizmoAllAxesRB = scaleAllAxes->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			gizmoAllAxesRB->SetMass(0.0f);
			gizmoAllAxesRB->SetKinematic(true);
			gizmoAllAxesRB->SetPhysicsFlags(gizmoRBFlags);

			allAxesMesh->LoadFromFile(MESH_DIRECTORY "scale-gizmo-all.glb", m_TransformGizmoMatAllID, false, nullptr, &gizmoCreateInfo);


			gizmoXAxisRB->SetLocalRotation(glm::quat(glm::vec3(0, 0, PI / 2.0f)));
			gizmoXAxisRB->SetLocalPosition(glm::vec3(cylinderHeight, 0, 0));

			gizmoYAxisRB->SetLocalPosition(glm::vec3(0, cylinderHeight, 0));

			gizmoZAxisRB->SetLocalRotation(glm::quat(glm::vec3(PI / 2.0f, 0, 0)));
			gizmoZAxisRB->SetLocalPosition(glm::vec3(0, 0, cylinderHeight));


			m_ScaleGizmo = new GameObject("Scale gizmo", GameObjectType::_NONE);

			m_ScaleGizmo->AddChildImmediate(scaleXAxis);
			m_ScaleGizmo->AddChildImmediate(scaleYAxis);
			m_ScaleGizmo->AddChildImmediate(scaleZAxis);
			m_ScaleGizmo->AddChildImmediate(scaleAllAxes);

			m_TransformGizmo->AddChildImmediate(m_ScaleGizmo);

			m_ScaleGizmo->SetVisible(false);
		}

		m_TransformGizmo->Initialize();

		m_TransformGizmo->PostInitialize();

		m_CurrentTransformGizmoState = TransformState::TRANSLATE;
	}

	void Editor::FadeOutHeadOnGizmos()
	{
		Transform* gizmoTransform = m_TransformGizmo->GetTransform();
		glm::vec3 centerToCam = glm::normalize(g_CameraManager->CurrentCamera()->position - gizmoTransform->GetWorldPosition());

		Material* xMat = g_Renderer->GetMaterial(m_TransformGizmoMatXID);
		Material* yMat = g_Renderer->GetMaterial(m_TransformGizmoMatYID);
		Material* zMat = g_Renderer->GetMaterial(m_TransformGizmoMatZID);

		real camViewXAlignment = glm::abs(glm::dot(centerToCam, gizmoTransform->GetRight()));
		real camViewYAlignment = glm::abs(glm::dot(centerToCam, gizmoTransform->GetUp()));
		real camViewZAlignment = glm::abs(glm::dot(centerToCam, gizmoTransform->GetForward()));

		if (m_CurrentTransformGizmoState == TransformState::ROTATE)
		{
			real threshold = 0.2f;
			real power = 0.05f;
			if (camViewXAlignment <= threshold)
			{
				xMat->colourMultiplier.a = Lerp(1.0f, 0.0f, glm::pow((threshold - camViewXAlignment) / threshold, power));
			}
			else
			{
				xMat->colourMultiplier.a = 1.0f;
			}
			if (camViewYAlignment <= threshold)
			{
				yMat->colourMultiplier.a = Lerp(1.0f, 0.0f, glm::pow((threshold - camViewYAlignment) / threshold, power));
			}
			else
			{
				yMat->colourMultiplier.a = 1.0f;
			}
			if (camViewZAlignment <= threshold)
			{
				zMat->colourMultiplier.a = Lerp(1.0f, 0.0f, glm::pow((threshold - camViewZAlignment) / threshold, power));
			}
			else
			{
				zMat->colourMultiplier.a = 1.0f;
			}
		}
		else
		{
			// TODO: Use different scheme for rotating when facing head-on (screen-space rather than world-space)
			real threshold = m_CurrentTransformGizmoState == TransformState::ROTATE ? 0.965f : 0.9f;
			real power = m_CurrentTransformGizmoState == TransformState::ROTATE ? 0.5f : 0.2f;
			if (camViewXAlignment >= threshold)
			{
				xMat->colourMultiplier.a = Lerp(1.0f, 0.0f, glm::pow((camViewXAlignment - threshold) / (1.0f - threshold), power));
			}
			else
			{
				xMat->colourMultiplier.a = 1.0f;
			}
			if (camViewYAlignment >= threshold)
			{
				yMat->colourMultiplier.a = Lerp(1.0f, 0.0f, glm::pow((camViewYAlignment - threshold) / (1.0f - threshold), power));
			}
			else
			{
				yMat->colourMultiplier.a = 1.0f;
			}
			if (camViewZAlignment >= threshold)
			{
				zMat->colourMultiplier.a = Lerp(1.0f, 0.0f, glm::pow((camViewZAlignment - threshold) / (1.0f - threshold), power));
			}
			else
			{
				zMat->colourMultiplier.a = 1.0f;
			}
		}
	}

	btVector3 Editor::GetAxisColour(i32 axisIndex) const
	{
		return axisIndex == X_AXIS_IDX ? btVector3(1.0f, 0.0f, 0.0f) : axisIndex == Y_AXIS_IDX ? btVector3(0.0f, 1.0f, 0.0f) : btVector3(0.1f, 0.1f, 1.0f);
	}
} // namespace flex
