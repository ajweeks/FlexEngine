#include "stdafx.hpp"

#include "Editor.hpp"

IGNORE_WARNINGS_PUSH
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletDynamics/Dynamics/btDynamicsWorld.h>

#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <glm/gtx/intersect.hpp>
#include <glm/gtx/common.hpp> // For fmod
#include <glm/gtx/euler_angles.hpp>
IGNORE_WARNINGS_POP

#include "Audio/AudioManager.hpp"
#include "Cameras/BaseCamera.hpp"
#include "Cameras/CameraManager.hpp"
#include "Cameras/DebugCamera.hpp"
#include "FlexEngine.hpp"
#include "Graphics/DebugRenderer.hpp"
#include "Graphics/Renderer.hpp"
#include "Graphics/RendererTypes.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Physics/PhysicsHelpers.hpp"
#include "Physics/PhysicsWorld.hpp"
#include "Physics/RigidBody.hpp"
#include "Platform/Platform.hpp"
#include "Player.hpp"
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
	const char* Editor::PrefabPayloadCStr = "prefab";
	const char* Editor::GameObjectPayloadCStr = "gameobject";
	const char* Editor::AudioFileNameSIDPayloadCStr = "audiosid";

	Editor::Editor() :
		m_MouseButtonCallback(this, &Editor::OnMouseButtonEvent),
		m_MouseMovedCallback(this, &Editor::OnMouseMovedEvent),
		m_KeyEventCallback(this, &Editor::OnKeyEvent),
		m_ActionCallback(this, &Editor::OnActionEvent),
		m_StartPointOnPlane(VEC3_ZERO),
		m_PreviousIntersectionPoint(VEC3_ZERO),
		m_AngleSnap(glm::radians(45.0f))
	{
		SelectNone();

		m_LMBDownPos = glm::vec2i(-1);
	}

	void Editor::Initialize()
	{
		PROFILE_AUTO("Editor Initialize");

		// Transform gizmo materials
		{
			MaterialCreateInfo matCreateInfo = {};
			matCreateInfo.shaderName = "colour";
			matCreateInfo.constAlbedo = VEC4_ONE;
			matCreateInfo.persistent = true;
			matCreateInfo.bEditorMaterial = true;
			matCreateInfo.bSerializable = false;
			matCreateInfo.name = "transform x";
			m_TransformGizmoMatXID = g_Renderer->InitializeMaterial(&matCreateInfo);
			matCreateInfo.name = "transform y";
			m_TransformGizmoMatYID = g_Renderer->InitializeMaterial(&matCreateInfo);
			matCreateInfo.name = "transform z";
			m_TransformGizmoMatZID = g_Renderer->InitializeMaterial(&matCreateInfo);
			matCreateInfo.name = "transform all";
			m_TransformGizmoMatAllID = g_Renderer->InitializeMaterial(&matCreateInfo);
			matCreateInfo.name = "transform yz";
			m_TransformGizmoMatYZID = g_Renderer->InitializeMaterial(&matCreateInfo);
			matCreateInfo.name = "transform xz";
			m_TransformGizmoMatXZID = g_Renderer->InitializeMaterial(&matCreateInfo);
			matCreateInfo.name = "transform xy";
			m_TransformGizmoMatXYID = g_Renderer->InitializeMaterial(&matCreateInfo);
		}

		g_InputManager->BindMouseButtonCallback(&m_MouseButtonCallback, 95);
		g_InputManager->BindMouseMovedCallback(&m_MouseMovedCallback, 15);
		g_InputManager->BindKeyEventCallback(&m_KeyEventCallback, 15);
		g_InputManager->BindActionCallback(&m_ActionCallback, 15);
	}

	void Editor::PostInitialize()
	{
		PROFILE_AUTO("Editor PostInitialize");

		CreateObjects();
	}

	void Editor::Destroy()
	{
		SelectNone();

		if (m_TransformGizmo != nullptr)
		{
			g_SceneManager->CurrentScene()->RemoveEditorObjectImmediate(m_TransformGizmo);
			m_TransformGizmo = nullptr;
		}

		if (m_GridObject != nullptr)
		{
			g_SceneManager->CurrentScene()->RemoveEditorObjectImmediate(m_GridObject);
			m_GridObject = nullptr;
		}

		g_InputManager->UnbindMouseButtonCallback(&m_MouseButtonCallback);
		g_InputManager->UnbindMouseMovedCallback(&m_MouseMovedCallback);
		g_InputManager->UnbindKeyEventCallback(&m_KeyEventCallback);
		g_InputManager->UnbindActionCallback(&m_ActionCallback);
	}

	void Editor::EarlyUpdate()
	{
		PROFILE_AUTO("Editor EarlyUpdate");

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
					g_Renderer->GetDebugRenderer()->DrawLineWithAlpha(p0, p1, colours[m_HoveringAxisIndex], colours[m_HoveringAxisIndex]);
				}
			}
		}
		else
		{
			m_bDraggingGizmo = false;
			if (!g_CameraManager->CurrentCamera()->bIsGameplayCam &&
				!m_CurrentlySelectedObjectIDs.empty())
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
			g_Renderer->GetDebugRenderer()->drawLine(
				btVector3(x, 0, z),
				g_Editor->HasSelectedObject() ? ToBtVec3(g_Editor->GetSelectedObjectsCenter()) : btVector3(x, 10, z),
				btVector3(sin(g_SecElapsedSinceProgramStart * 5.0f) * 0.5f + 0.5f, cos(g_SecElapsedSinceProgramStart * 2.5f) * 0.5f + 0.5f, 1.0f),
				btVector3(1.0f, 0.0f, sin(g_SecElapsedSinceProgramStart * 5.0f) * 0.5f + 0.5f));
		}
#endif

		FadeOutHeadOnGizmos();

		if (!m_CurrentlySelectedObjectIDs.empty() && !g_CameraManager->CurrentCamera()->bIsGameplayCam)
		{
			m_TransformGizmo->SetVisible(true);
			UpdateGizmoVisibility();
			CalculateSelectedObjectsCenter();
			Transform* gizmoTransform = m_TransformGizmo->GetTransform();
			if (!NearlyEquals(gizmoTransform->GetLocalPosition(), m_SelectedObjectsCenterPos, 0.0001f))
			{
				gizmoTransform->SetLocalPosition(m_SelectedObjectsCenterPos, true);
			}
			if (!NearlyEquals(gizmoTransform->GetLocalRotation(), m_SelectedObjectRotation, 0.0001f))
			{
				gizmoTransform->SetLocalRotation(m_SelectedObjectRotation, true);
			}

			glm::vec3 camPos = g_CameraManager->CurrentCamera()->position;
			real scale = glm::max(glm::distance(gizmoTransform->GetWorldPosition(), camPos) / 80.0f + 0.5f, 0.1f);
			if (!NearlyEquals(scale, gizmoTransform->GetLocalScale().x, 0.0001f))
			{
				gizmoTransform->SetWorldScale(glm::vec3(scale));
			}
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

	void Editor::FixedUpdate()
	{
		m_JitterDetector.FixedUpdate();
	}

	void Editor::PreSceneChange()
	{
		if (m_TransformGizmo != nullptr)
		{
			g_SceneManager->CurrentScene()->RemoveEditorObjectImmediate(m_TransformGizmo);
			m_TransformGizmo = nullptr;
		}

		if (m_GridObject != nullptr)
		{
			g_SceneManager->CurrentScene()->RemoveEditorObjectImmediate(m_GridObject);
			m_GridObject = nullptr;
		}

		SelectNone();
	}

	void Editor::OnSceneChanged()
	{
		CreateObjects();
	}

	void Editor::DrawImGuiObjects()
	{
		m_JitterDetector.DrawImGuiObjects();
	}

	bool Editor::HasSelectedObject() const
	{
		return !m_CurrentlySelectedObjectIDs.empty();
	}

	bool Editor::HasSelectedEditorObject() const
	{
		return m_CurrentlySelectedEditorObjectID != InvalidEditorObjectID;
	}

	std::vector<GameObjectID> Editor::GetSelectedObjectIDs(bool bForceIncludeChildren) const
	{
		if (bForceIncludeChildren)
		{
			std::vector<GameObjectID> selectedObjects;
			BaseScene* currentScene = g_SceneManager->CurrentScene();
			for (const GameObjectID& gameObjectID : m_CurrentlySelectedObjectIDs)
			{
				GameObject* gameObject = currentScene->GetGameObject(gameObjectID);
				gameObject->AddSelfIDAndChildrenToVec(selectedObjects);
			}
			return selectedObjects;
		}
		else
		{
			return m_CurrentlySelectedObjectIDs;
		}
	}

	GameObjectID Editor::GetFirstSelectedObjectID() const
	{
		return m_CurrentlySelectedObjectIDs[0];
	}

	void Editor::SetSelectedObject(const GameObjectID& gameObjectID, bool bSelectChildren /* = false */)
	{
		SelectNone();

		GameObject* gameObject = g_SceneManager->CurrentScene()->GetGameObject(gameObjectID);
		if (gameObject != nullptr)
		{
			if (bSelectChildren)
			{
				gameObject->AddSelfIDAndChildrenToVec(m_CurrentlySelectedObjectIDs);
			}
			else
			{
				m_CurrentlySelectedObjectIDs.push_back(gameObjectID);
			}
		}

		CalculateSelectedObjectsCenter();
	}

	void Editor::SetSelectedObjects(const std::vector<GameObjectID>& selectedObjects)
	{
		SelectNone();

		m_CurrentlySelectedObjectIDs = selectedObjects;

		CalculateSelectedObjectsCenter();
	}

	void Editor::ToggleSelectedObject(const GameObjectID& gameObjectID)
	{
		auto iter = FindIter(m_CurrentlySelectedObjectIDs, gameObjectID);
		if (iter == m_CurrentlySelectedObjectIDs.end())
		{
			GameObject* gameObject = g_SceneManager->CurrentScene()->GetGameObject(gameObjectID);
			gameObject->AddSelfIDAndChildrenToVec(m_CurrentlySelectedObjectIDs);
		}
		else
		{
			m_CurrentlySelectedObjectIDs.erase(iter);

			if (m_CurrentlySelectedObjectIDs.empty())
			{
				SelectNone();
			}
		}

		CalculateSelectedObjectsCenter();
	}

	void Editor::AddSelectedObject(const GameObjectID& gameObjectID)
	{
		if (!Contains(m_CurrentlySelectedObjectIDs, gameObjectID))
		{
			GameObject* gameObject = g_SceneManager->CurrentScene()->GetGameObject(gameObjectID);
			if (gameObject != nullptr)
			{
				gameObject->AddSelfIDAndChildrenToVec(m_CurrentlySelectedObjectIDs);

				CalculateSelectedObjectsCenter();
			}
		}
	}

	void Editor::DeselectObject(const GameObjectID& gameObjectID)
	{
		for (const GameObjectID& selectedObj : m_CurrentlySelectedObjectIDs)
		{
			if (selectedObj == gameObjectID)
			{
				GameObject* gameObject = g_SceneManager->CurrentScene()->GetGameObject(gameObjectID);
				if (gameObject != nullptr)
				{
					gameObject->RemoveSelfIDAndChildrenFromVec(m_CurrentlySelectedObjectIDs);
					CalculateSelectedObjectsCenter();
				}
				else
				{
					Erase(m_CurrentlySelectedObjectIDs, gameObjectID);
				}

				return;
			}
		}

		PrintWarn("Attempted to deselect object which wasn't selected!\n");
	}

	bool Editor::IsObjectSelected(const GameObjectID& gameObjectID)
	{
		bool bSelected = Contains(m_CurrentlySelectedObjectIDs, gameObjectID);
		return bSelected;
	}

	glm::vec3 Editor::GetSelectedObjectsCenter()
	{
		return m_SelectedObjectsCenterPos;
	}

	void Editor::SelectNone()
	{
		m_CurrentlySelectedObjectIDs.clear();
		m_SelectedObjectsCenterPos = VEC3_ZERO;
		m_SelectedObjectDragStartPos = VEC3_ZERO;
		m_DraggingGizmoScaleLast1 = VEC3_ZERO;
		m_DraggingGizmoScaleLast2 = VEC3_ZERO;
		m_DraggingGizmoOffset = 0.0f;
		m_DraggingGizmoOffset2D = VEC2_ZERO;
		m_DraggingAxisIndex = -1;
		m_bDraggingGizmo = false;
	}

	void Editor::SetSelectedEditorObject(EditorObjectID* editorObjectID)
	{
		m_CurrentlySelectedEditorObjectID = *editorObjectID;
	}

	bool Editor::IsEditorObjectSelected(EditorObjectID* editorObjectID)
	{
		return m_CurrentlySelectedEditorObjectID == *editorObjectID;
	}

	EditorObjectID Editor::GetSelectedEditorObject() const
	{
		return m_CurrentlySelectedEditorObjectID;
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
		if (m_CurrentlySelectedObjectIDs.empty())
		{
			return;
		}

		glm::vec3 avgPos(0.0f);
		BaseScene* currentScene = g_SceneManager->CurrentScene();
		for (const GameObjectID& gameObjectID : m_CurrentlySelectedObjectIDs)
		{
			GameObject* gameObject = currentScene->GetGameObject(gameObjectID);
			// Object may not have been added to scene yet (e.g. during obj duplication), silently fail
			if (gameObject != nullptr)
			{
				avgPos += gameObject->GetTransform()->GetWorldPosition();
			}
		}
		m_SelectedObjectsCenterPos = m_SelectedObjectDragStartPos;
		GameObject* firstObject = currentScene->GetGameObject(m_CurrentlySelectedObjectIDs[0]);
		m_SelectedObjectRotation = (firstObject != nullptr ? firstObject->GetTransform()->GetWorldRotation() : QUAT_IDENTITY);

		m_SelectedObjectsCenterPos = (avgPos / (real)m_CurrentlySelectedObjectIDs.size());
	}

	void Editor::SelectAll()
	{
		std::vector<GameObject*> allObjects = g_SceneManager->CurrentScene()->GetAllObjects();
		for (GameObject* gameObject : allObjects)
		{
			m_CurrentlySelectedObjectIDs.push_back(gameObject->ID);
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
		GameObject* pickedTransformGizmo = physicsWorld->PickTaggedBody(rayStart, rayEnd, gizmoTag, (u32)CollisionType::EDITOR_OBJECT);

		Material* xMat = g_Renderer->GetMaterial(m_TransformGizmoMatXID);
		Material* yMat = g_Renderer->GetMaterial(m_TransformGizmoMatYID);
		Material* zMat = g_Renderer->GetMaterial(m_TransformGizmoMatZID);
		Material* allMat = g_Renderer->GetMaterial(m_TransformGizmoMatAllID);
		Material* yzMat = g_Renderer->GetMaterial(m_TransformGizmoMatYZID);
		Material* xzMat = g_Renderer->GetMaterial(m_TransformGizmoMatXZID);
		Material* xyMat = g_Renderer->GetMaterial(m_TransformGizmoMatXYID);
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

				std::vector<GameObject*> translationPlanes = m_TranslationGizmoPlanes->GetChildren();

				if (pickedTransformGizmo == translationPlanes[X_AXIS_IDX] && yzMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = YZ_AXIS_IDX;
					yzMat->colourMultiplier = hoverColour;
				}
				else if (pickedTransformGizmo == translationPlanes[Y_AXIS_IDX] && xzMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = XZ_AXIS_IDX;
					xzMat->colourMultiplier = hoverColour;
				}
				else if (pickedTransformGizmo == translationPlanes[Z_AXIS_IDX] && xyMat->colourMultiplier.a > alphaThreshold)
				{
					m_HoveringAxisIndex = XY_AXIS_IDX;
					xyMat->colourMultiplier = hoverColour;
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
		if (m_DraggingAxisIndex != YZ_AXIS_IDX && m_HoveringAxisIndex != YZ_AXIS_IDX)
		{
			yzMat->colourMultiplier = white;
		}
		if (m_DraggingAxisIndex != XZ_AXIS_IDX && m_HoveringAxisIndex != XZ_AXIS_IDX)
		{
			xzMat->colourMultiplier = white;
		}
		if (m_DraggingAxisIndex != XY_AXIS_IDX && m_HoveringAxisIndex != XY_AXIS_IDX)
		{
			xyMat->colourMultiplier = white;
		}

		return m_HoveringAxisIndex != -1;
	}

	void Editor::HandleGizmoClick()
	{
		CHECK_EQ(m_bDraggingGizmo, false);
		CHECK_NE(m_HoveringAxisIndex, -1);

		m_DraggingAxisIndex = m_HoveringAxisIndex;
		m_bDraggingGizmo = true;
		m_SelectedObjectDragStartPos = m_TransformGizmo->GetTransform()->GetWorldPosition();

		real gizmoSelectedMultiplier = 0.4f;
		glm::vec4 selectedColour(gizmoSelectedMultiplier, gizmoSelectedMultiplier, gizmoSelectedMultiplier, 1.0f);

		Material* xMat = g_Renderer->GetMaterial(m_TransformGizmoMatXID);
		Material* yMat = g_Renderer->GetMaterial(m_TransformGizmoMatYID);
		Material* zMat = g_Renderer->GetMaterial(m_TransformGizmoMatZID);
		Material* allMat = g_Renderer->GetMaterial(m_TransformGizmoMatAllID);
		Material* yzMat = g_Renderer->GetMaterial(m_TransformGizmoMatYZID);
		Material* xzMat = g_Renderer->GetMaterial(m_TransformGizmoMatXZID);
		Material* xyMat = g_Renderer->GetMaterial(m_TransformGizmoMatXYID);

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
			else if (m_HoveringAxisIndex == YZ_AXIS_IDX)
			{
				yzMat->colourMultiplier = selectedColour;
			}
			else if (m_HoveringAxisIndex == XZ_AXIS_IDX)
			{
				xzMat->colourMultiplier = selectedColour;
			}
			else if (m_HoveringAxisIndex == XY_AXIS_IDX)
			{
				xyMat->colourMultiplier = selectedColour;
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
			m_DraggingGizmoOffsetNeedsRecalculation = true;
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
			m_DraggingGizmoOffsetNeedsRecalculation = true;
		} break;
		default:
		{
			PrintError("Unhandled transform state in Editor::HandleGizmoClick\n");
		} break;
		}
	}

	void Editor::HandleGizmoMovement()
	{
		CHECK_NE(m_HoveringAxisIndex, -1);
		CHECK_NE(m_DraggingAxisIndex, -1);
		CHECK(m_bDraggingGizmo);
		if (m_CurrentlySelectedObjectIDs.empty())
		{
			SelectNone();
			return;
		}

		Transform* gizmoTransform = m_TransformGizmo->GetTransform();

		const glm::vec3 gizmoUp = gizmoTransform->GetUp();
		const glm::vec3 gizmoRight = gizmoTransform->GetRight();
		const glm::vec3 gizmoForward = gizmoTransform->GetForward();

		btVector3 btRayStart, btRayEnd;
		FlexEngine::GenerateRayAtMousePos(btRayStart, btRayEnd);

		glm::vec3 rayStart = ToVec3(btRayStart);
		glm::vec3 rayEnd = ToVec3(btRayEnd);
		BaseCamera* cam = g_CameraManager->CurrentCamera();
		glm::vec3 camForward = cam->forward;
		glm::vec3 camRight = cam->right;
		glm::vec3 camUp = cam->up;
		glm::vec3 planeOrigin = gizmoTransform->GetWorldPosition();
		BaseScene* currentScene = g_SceneManager->CurrentScene();
		Transform* transform = currentScene->GetGameObject(m_CurrentlySelectedObjectIDs[0])->GetTransform();

		if (g_InputManager->DidMouseWrap())
		{
			m_DraggingGizmoOffsetNeedsRecalculation = true; // Signal to recalculate offset in CalculateRayPlaneIntersectionAlongAxis

			m_SelectedObjectDragStartPos = gizmoTransform->GetWorldPosition();
			if (m_CurrentTransformGizmoState == TransformState::ROTATE)
			{
				m_bFirstFrameDraggingRotationGizmo = true;
			}
		}

		switch (m_CurrentTransformGizmoState)
		{
		case TransformState::TRANSLATE:
		{
			// Axes for translating along X, Y, and Z respectively
			const glm::vec3* axes[] = { &gizmoRight, &gizmoUp, &gizmoForward };

			const bool bDraggingPlane = m_DraggingAxisIndex > Z_AXIS_IDX;

			glm::vec3 deltaPosWS;

			if (bDraggingPlane)
			{
				u32 axisIdx = m_DraggingAxisIndex - YZ_AXIS_IDX;

				glm::vec3 planeN = *axes[axisIdx];
				glm::vec3 planeT = *axes[(axisIdx + 1) % 3];
				glm::vec3 planeB = *axes[(axisIdx + 2) % 3];
				glm::vec3 intersectionPont = FlexEngine::CalculateRayPlaneIntersection(
					rayStart,
					rayEnd,
					planeOrigin,
					planeN,
					planeT,
					planeB,
					m_SelectedObjectDragStartPos,
					camForward,
					m_DraggingGizmoOffset2D,
					m_DraggingGizmoOffsetNeedsRecalculation,
					m_PreviousIntersectionPoint);

				// Dragging along plane
				deltaPosWS = (intersectionPont - planeOrigin);
			}
			else
			{
				const glm::vec3* altAxes[] = { &gizmoUp, &gizmoForward, &gizmoRight };
				const glm::vec3* planeNormals[] = { &gizmoForward, &gizmoRight, &gizmoUp };

				// Dragging along single axis
				u32 axisIdx = m_DraggingAxisIndex;
				glm::vec3 axis = *axes[axisIdx];
				glm::vec3 planeN = *planeNormals[axisIdx];
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = *altAxes[axisIdx];
				}

				glm::vec3 constrainedIntersectionPont = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(
					axis,
					rayStart,
					rayEnd,
					planeOrigin,
					planeN,
					m_SelectedObjectDragStartPos,
					camForward,
					m_DraggingGizmoOffset,
					m_DraggingGizmoOffsetNeedsRecalculation,
					m_PreviousIntersectionPoint);
				deltaPosWS = (constrainedIntersectionPont - planeOrigin);
			}

			glm::vec3 deltaPos = transform->GetLocalRotation() * glm::inverse(transform->GetWorldRotation()) * deltaPosWS;

			if (deltaPos != VEC3_ZERO)
			{
				for (const GameObjectID& gameObjectID : m_CurrentlySelectedObjectIDs)
				{
					GameObject* gameObject = currentScene->GetGameObject(gameObjectID);
					GameObject* parent = gameObject->GetParent();
					bool bObjectIsntChild = (parent == nullptr) || !Contains(m_CurrentlySelectedObjectIDs, parent->ID);
					if (bObjectIsntChild)
					{
						gameObject->GetTransform()->Translate(deltaPos);
					}
				}
			}
		} break;
		case TransformState::ROTATE:
		{
			// Axes for rotating about X, Y, and Z respectively
			const glm::vec3* axesProjectedOnto[] = { &gizmoUp, &gizmoRight, &gizmoUp };
			const glm::vec3* axesOfRotation[] = { &gizmoRight, &gizmoUp, &gizmoForward };
			static glm::vec3 planeN = VEC3_UP;

			static bool bCtrlWasDown = false;
			bool bCtrlDown = g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_CONTROL);

			if (m_bFirstFrameDraggingRotationGizmo)
			{
				m_AxisProjectedOnto = *axesProjectedOnto[m_DraggingAxisIndex];
				m_AxisOfRotation = *axesOfRotation[m_DraggingAxisIndex];
				planeN = m_AxisOfRotation;
				if (glm::abs(glm::dot(m_AxisProjectedOnto, camForward)) > 0.5f)
				{
					m_AxisProjectedOnto = gizmoForward;
				}
			}

			glm::vec3 intersectionPoint(0.0f);

			glm::vec3 rayDir = glm::normalize(rayEnd - rayStart);
			if (glm::dot(planeN, camForward) > 0.0f)
			{
				planeN = -planeN;
			}
			real intersectionDistance;
			if (glm::intersectRayPlane(rayStart, rayDir, planeOrigin, planeN, intersectionDistance))
			{
				intersectionPoint = rayStart + rayDir * intersectionDistance;
				if (m_DraggingGizmoOffset == -1.0f)
				{
					m_DraggingGizmoOffset = glm::dot(intersectionPoint - m_SelectedObjectDragStartPos, m_AxisProjectedOnto);
				}

				if (m_bFirstFrameDraggingRotationGizmo)
				{
					m_StartPointOnPlane = intersectionPoint;
				}
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

			real angleAbs = acos(projectedDiff);
			real angle = (m_RotationGizmoWrapCount % 2 == 0) ? -angleAbs : angleAbs;

			if (m_bFirstFrameDraggingRotationGizmo)
			{
				m_LastAngle = angle;
			}

			static real currentAngleExtra = 0.0f;
			if (bCtrlDown)
			{
				//Print("## extra: %.2f, angle: %.2f\n", glm::degrees(currentAngleExtra), glm::degrees(angle));

				Transform* objTransform = currentScene->GetGameObject(m_CurrentlySelectedObjectIDs[0])->GetTransform();
				glm::quat firstSelectedObjRotOS = objTransform->GetLocalRotation();
				real objAngle = glm::eulerAngles(firstSelectedObjRotOS)[m_DraggingAxisIndex];

				if (!bCtrlWasDown || m_bFirstFrameDraggingRotationGizmo)
				{
					// TODO: This doesn't properly handle all cases, figure out why
					currentAngleExtra = glm::round((objAngle) / m_AngleSnap) * m_AngleSnap - objAngle;
					//Print("-- cur: %.2f, extra: %.2f, angle: %.2f, last: %.2f\n", glm::degrees(objAngle), glm::degrees(currentAngleExtra), glm::degrees(angle), glm::degrees(m_LastAngle));
				}

				angle = glm::round(angle / m_AngleSnap) * m_AngleSnap;
				angle += currentAngleExtra;

				//real dAngle = (angle - m_LastAngle);
				//Print("   cur: %.2f, extra: %.2f, angle: %.2f, dAngle: %.2f\n", glm::degrees(objAngle), glm::degrees(currentAngleExtra), glm::degrees(angle), glm::degrees(dAngle));
			}
			else
			{
				currentAngleExtra = 0.0f;
			}

			if (m_bFirstFrameDraggingRotationGizmo)
			{
				m_bFirstFrameDraggingRotationGizmo = false;
			}

			real dAngle = angle - m_LastAngle;
			m_LastAngle = angle;

			if (dAngle != 0.0f)
			{
				for (const GameObjectID& gameObjectID : m_CurrentlySelectedObjectIDs)
				{
					GameObject* gameObject = currentScene->GetGameObject(gameObjectID);
					GameObject* parent = gameObject->GetParent();
					bool bObjectIsntChild = (parent == nullptr) || !Contains(m_CurrentlySelectedObjectIDs, parent->ID);
					if (bObjectIsntChild)
					{
						Transform* t = gameObject->GetTransform();
						glm::vec3 axis = parent != nullptr ? (glm::inverse(parent->GetTransform()->GetWorldRotation()) * m_AxisOfRotation) : m_AxisOfRotation;
						glm::quat dRot = glm::angleAxis(dAngle, axis);
						t->SetLocalRotation(dRot* t->GetLocalRotation());
					}
				}
			}

			bCtrlWasDown = bCtrlDown;
		} break;
		case TransformState::SCALE:
		{
			auto CalculateRayPlaneIntersection = [&](const glm::vec3& axis, const glm::vec3& planeN) {
				return FlexEngine::CalculateRayPlaneIntersectionAlongAxis(
					axis,
					rayStart,
					rayEnd,
					planeOrigin,
					planeN,
					m_SelectedObjectDragStartPos,
					camForward,
					m_DraggingGizmoOffset,
					m_DraggingGizmoOffsetNeedsRecalculation,
					m_PreviousIntersectionPoint);
			};

			glm::vec3 dLocalScale(1.0f);
			bool bControlDown = g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_CONTROL);
			bool bShiftDown = g_InputManager->GetKeyDown(KeyCode::KEY_LEFT_SHIFT);
			real dragSpeed = m_ScaleDragSpeed * (bControlDown ? m_ScaleSlowDragSpeedMultiplier : bShiftDown ? m_ScaleFastDragSpeedMultiplier : 1.0f);
			if (m_DraggingAxisIndex == ALL_AXES_IDX)
			{
				glm::vec3 axis1 = -camRight;
				glm::vec3 axis2 = camUp;
				glm::vec3 planeN = -camForward;

				glm::vec3 intersectionPont1 = CalculateRayPlaneIntersection(axis1, planeN);
				glm::vec3 intersectionPont2 = CalculateRayPlaneIntersection(axis2, planeN);
				real dotResult1 = glm::dot(intersectionPont1 - planeOrigin, axis1);
				real dotResult2 = glm::dot(intersectionPont2 - planeOrigin, axis2);

				glm::vec3 absoluteScale = glm::vec3(glm::clamp(dotResult1, -9999.0f, 9999.0f));

				if (m_DraggingGizmoOffsetNeedsRecalculation)
				{
					m_DraggingGizmoScaleLast1 = absoluteScale;
				}

				glm::vec3 deltaScale = glm::sign(dotResult1) * (glm::abs(absoluteScale) - glm::abs(m_DraggingGizmoScaleLast1)) * dragSpeed;
				dLocalScale += glm::vec3(glm::vec4(deltaScale, 0.0f));
				m_DraggingGizmoScaleLast1 = absoluteScale;

				absoluteScale = glm::vec3(glm::clamp(dotResult2, -9999.0f, 9999.0f));

				if (m_DraggingGizmoOffsetNeedsRecalculation)
				{
					m_DraggingGizmoScaleLast2 = absoluteScale;
				}

				deltaScale = glm::sign(dotResult2) * (glm::abs(absoluteScale) - glm::abs(m_DraggingGizmoScaleLast2)) * dragSpeed;
				dLocalScale += glm::vec3(glm::vec4(deltaScale, 0.0f));
				m_DraggingGizmoScaleLast2 = absoluteScale;
			}
			else
			{
				// Axes for scaling along X, Y, and Z respectively
				const glm::vec3* axes[] = { &gizmoRight, &gizmoUp, &gizmoForward };
				const glm::vec3* altAxes[] = { &gizmoUp, &gizmoForward, &gizmoRight };
				const glm::vec3* planeNormals[] = { &gizmoForward, &gizmoRight, &gizmoUp };

				glm::mat4 inverseTransform = glm::inverse(gizmoTransform->GetWorldTransform());

				glm::vec3 axis = *axes[m_DraggingAxisIndex];
				glm::vec3 planeN = *planeNormals[m_DraggingAxisIndex];
				if (glm::abs(glm::dot(planeN, camForward)) < 0.5f)
				{
					planeN = *altAxes[m_DraggingAxisIndex];
				}
				glm::vec3 intersectionPont = CalculateRayPlaneIntersection(axis, planeN);
				glm::vec3 absoluteScale = (intersectionPont - planeOrigin);

				if (m_DraggingGizmoOffsetNeedsRecalculation)
				{
					m_DraggingGizmoScaleLast1 = absoluteScale;
				}

				glm::vec3 deltaScale = (absoluteScale - m_DraggingGizmoScaleLast1) * dragSpeed;
				dLocalScale += glm::vec3(inverseTransform * glm::vec4(deltaScale, 0.0f));

				m_DraggingGizmoScaleLast1 = absoluteScale;
			}

			dLocalScale = glm::clamp(dLocalScale, 0.001f, 10.0f);

			if (dLocalScale != VEC3_ONE)
			{
				for (const GameObjectID& gameObjectID : m_CurrentlySelectedObjectIDs)
				{
					GameObject* gameObject = currentScene->GetGameObject(gameObjectID);
					GameObject* parent = gameObject->GetParent();
					bool bObjectIsntChild = (parent == nullptr) || (!Contains(m_CurrentlySelectedObjectIDs, parent->ID));
					if (bObjectIsntChild)
					{
						gameObject->GetTransform()->Scale(dLocalScale);
						if (gameObject->HasUniformScale())
						{
							gameObject->EnforceUniformScale();
						}
					}
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
					ToggleSelectedObject(pickedObject->ID);
				}
				else
				{
					SelectNone();
					m_CurrentlySelectedObjectIDs.push_back(pickedObject->ID);
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
		std::vector<GameObjectID> createdObjs;

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
					EditorObject* newObj = new EditorObject(scene->GetUniqueObjectName("New mesh ", 1));

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
					createdObjs.push_back(newObj->ID);
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
		BaseCamera* cam = g_CameraManager->CurrentCamera();
		if (cam->bIsGameplayCam)
		{
			return EventReply::UNCONSUMED;
		}

		if (button == MouseButton::LEFT)
		{
			if (action == KeyAction::KEY_PRESS)
			{
				m_LMBDownPos = g_InputManager->GetMousePosition();

				if (m_HoveringAxisIndex != -1)
				{
					HandleGizmoClick();
					return EventReply::CONSUMED;
				}

				return EventReply::UNCONSUMED;
			}
			else if (action == KeyAction::KEY_RELEASE)
			{
				if (m_bDraggingGizmo)
				{
					if (m_CurrentlySelectedObjectIDs.empty())
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
					m_DraggingGizmoScaleLast1 = VEC3_ZERO;
					m_DraggingGizmoScaleLast2 = VEC3_ZERO;
					m_DraggingGizmoOffset = 0.0f;
					m_DraggingGizmoOffset2D = VEC2_ZERO;
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

		BaseCamera* cam = g_CameraManager->CurrentCamera();
		if (cam->bIsGameplayCam)
		{
			return EventReply::UNCONSUMED;
		}

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

		if (bControlDown && action == KeyAction::KEY_PRESS && keyCode == KeyCode::KEY_S)
		{
			g_SceneManager->CurrentScene()->SerializeToFile(true);
			return EventReply::CONSUMED;
		}

		BaseCamera* cam = g_CameraManager->CurrentCamera();
		if (cam->bIsGameplayCam)
		{
			return EventReply::UNCONSUMED;
		}

		if (action == KeyAction::KEY_PRESS)
		{
			if (keyCode == KeyCode::KEY_DELETE)
			{
				if (!m_CurrentlySelectedObjectIDs.empty())
				{
					g_SceneManager->CurrentScene()->RemoveObjects(m_CurrentlySelectedObjectIDs, true);

					SelectNone();
					return EventReply::CONSUMED;
				}
			}

			if (bControlDown && keyCode == KeyCode::KEY_D)
			{
				if (!m_CurrentlySelectedObjectIDs.empty())
				{
					std::vector<GameObjectID> newSelectedGameObjectIDs;

					BaseScene* currentScene = g_SceneManager->CurrentScene();
					for (const GameObjectID& gameObjectID : m_CurrentlySelectedObjectIDs)
					{
						GameObject* gameObject = currentScene->GetGameObject(gameObjectID);
						GameObject* parent = gameObject->GetParent();

						if (parent == nullptr || !Contains(m_CurrentlySelectedObjectIDs, parent->ID))
						{
							GameObject* duplicatedObject = gameObject->CopySelf();

							if (duplicatedObject != nullptr)
							{
								duplicatedObject->AddSelfIDAndChildrenToVec(newSelectedGameObjectIDs);
								duplicatedObject->Initialize();
								duplicatedObject->PostInitialize();
							}
						}
					}

					SetSelectedObjects(newSelectedGameObjectIDs);
				}
				return EventReply::CONSUMED;
			}


			if (bControlDown && keyCode == KeyCode::KEY_A)
			{
				SelectAll();
				return EventReply::CONSUMED;
			}

			if (g_InputManager->GetKeyPressed(KeyCode::KEY_END) && !m_CurrentlySelectedObjectIDs.empty())
			{
				BaseScene* currentScene = g_SceneManager->CurrentScene();
				GameObject* firstObject = currentScene->GetGameObject(m_CurrentlySelectedObjectIDs[0]);

				btCollisionShape* collisionShape = firstObject->GetCollisionShape();
				if (collisionShape != nullptr && collisionShape->isConvex())
				{
					btTransform from(ToBtQuaternion(m_SelectedObjectRotation), ToBtVec3(m_SelectedObjectsCenterPos));
					btTransform to(ToBtQuaternion(m_SelectedObjectRotation), ToBtVec3(m_SelectedObjectsCenterPos - VEC3_UP * 10000.0f));
					glm::vec3 pointOnGround, groundNormal;
					if (currentScene->GetPhysicsWorld()->GetPointOnGround((btConvexShape*)collisionShape, from, to, pointOnGround, groundNormal))
					{
						AABB collisionAABB;
						if (firstObject->GetCollisionAABB(collisionAABB))
						{
							real dPosY = pointOnGround.y - (firstObject->GetTransform()->GetWorldPosition().y + collisionAABB.minY);
							glm::vec3 dPos(0.0f, dPosY, 0.0f);

							for (const GameObjectID& gameObjectID : m_CurrentlySelectedObjectIDs)
							{
								GameObject* gameObject = currentScene->GetGameObject(gameObjectID);
								GameObject* parent = gameObject->GetParent();
								bool bObjectIsntChild = (parent == nullptr) || !Contains(m_CurrentlySelectedObjectIDs, parent->ID);
								if (bObjectIsntChild)
								{
									gameObject->GetTransform()->Translate(dPos);
								}
							}
						}
					}
				}

				return EventReply::CONSUMED;
			}
		}

		return EventReply::UNCONSUMED;
	}

	EventReply Editor::OnActionEvent(Action action, ActionEvent actionEvent)
	{
		BaseCamera* cam = g_CameraManager->CurrentCamera();
		if (cam->bIsGameplayCam)
		{
			return EventReply::UNCONSUMED;
		}

		if (actionEvent == ActionEvent::ACTION_TRIGGER)
		{
			if (action == Action::EDITOR_RENAME_SELECTED)
			{
				m_bWantRenameActiveElement = !m_bWantRenameActiveElement;
				return EventReply::CONSUMED;
			}

			// TODO: Check for exact matches, don't match if additional modifiers are down
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
				if (!m_CurrentlySelectedObjectIDs.empty())
				{
					SelectNone();
				}
				return EventReply::CONSUMED;
			}

			CameraType currentCameraType = g_CameraManager->CurrentCamera()->type;
			if (action == Action::EDITOR_FOCUS_ON_SELECTION && !m_CurrentlySelectedObjectIDs.empty() && currentCameraType == CameraType::DEBUG_CAM)
			{
				BaseScene* currentScene = g_SceneManager->CurrentScene();

				glm::vec3 minPos(FLT_MAX);
				glm::vec3 maxPos(-FLT_MAX);
				for (const GameObjectID& gameObjectID : m_CurrentlySelectedObjectIDs)
				{
					GameObject* gameObject = currentScene->GetGameObject(gameObjectID);
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
		}

		return EventReply::UNCONSUMED;
	}

	void Editor::CreateObjects()
	{
		if (m_TransformGizmo != nullptr)
		{
			return;
		}

		BaseScene* currentScene = g_SceneManager->CurrentScene();

		RenderObjectCreateInfo gizmoCreateInfo = {};
		gizmoCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
		gizmoCreateInfo.bEditorObject = true;
		gizmoCreateInfo.bSetDynamicStates = true;
		gizmoCreateInfo.cullFace = CullFace::BACK;

		m_GridObject = new EditorObject("Grid");
		Mesh* gridMesh = m_GridObject->SetMesh(new Mesh(m_GridObject));
		RenderObjectCreateInfo gridCreateInfo = {};
		gridCreateInfo.bEditorObject = true;
		gridMesh->LoadPrefabShape(PrefabShape::GRID, m_TransformGizmoMatAllID, &gridCreateInfo);
		m_GridObject->Initialize();
		m_GridObject->PostInitialize();
		m_GridObject->SetVisible(m_bShowGrid);

		currentScene->AddEditorObjectImmediate(m_GridObject);

		m_TransformGizmo = new EditorObject("Transform gizmo");

		u32 gizmoRBFlags = ((u32)PhysicsFlag::TRIGGER) | ((u32)PhysicsFlag::UNSELECTABLE);
		u32 gizmoRBGroup = (u32)CollisionType::EDITOR_OBJECT;
		u32 gizmoRBMask = (u32)CollisionType::EDITOR_OBJECT;

		Mesh::CreateInfo meshCreateInfo = {};
		meshCreateInfo.optionalRenderObjectCreateInfo = &gizmoCreateInfo;

		m_TranslationGizmo = new EditorObject("Translation gizmo");
		m_TranslationGizmoPlanes = new EditorObject("Translation gizmo planes");
		m_RotationGizmo = new EditorObject("Rotation gizmo");
		m_ScaleGizmo = new EditorObject("Scale gizmo");

		btVector3 translationShapeSize = btVector3(0.35f, 1.8f, 0.35f);
		btVector3 translationPlaneShapeSize = btVector3(0.75f, 0.75f, 0.05f);
		real translationPlaneOffset = 3.0f;
		btVector3 rotationShapeSize = btVector3(3.4f, 0.2f, 3.4f);
		btVector3 scaleShapeSize = btVector3(0.35f, 1.8f, 0.35f);
		btVector3 scaleAllShapeSize = btVector3(0.5f, 0.5f, 0.5f);

		struct MeshCreateInfo
		{
			const char* name;
			const char* meshName;
			glm::quat orientation;
			glm::vec3 offset;
			btCollisionShape* collisionShape;
			MaterialID matID;
			const std::string* tag;
			EditorObject* parent;
		};

		MeshCreateInfo translationCreateInfos[] = {
			{
				"Translation gizmo x axis",
				MESH_DIRECTORY "translation-gizmo-x.glb",
				glm::quat(glm::vec3(0, 0, -PI_DIV_TWO)),
				glm::vec3(translationShapeSize.y(), 0, 0),
				new btCylinderShape(translationShapeSize),
				m_TransformGizmoMatXID,
				&m_TranslationGizmoTag,
				m_TranslationGizmo
			},
			{
				"Translation gizmo y axis",
				MESH_DIRECTORY "translation-gizmo-y.glb",
				glm::quat(glm::vec3(0, 0, 0)),
				glm::vec3(0, translationShapeSize.y(), 0),
				new btCylinderShape(translationShapeSize),
				m_TransformGizmoMatYID,
				&m_TranslationGizmoTag,
				m_TranslationGizmo
			},
			{
				"Translation gizmo z axis",
				MESH_DIRECTORY "translation-gizmo-z.glb",
				glm::quat(glm::vec3(PI_DIV_TWO, 0, 0)),
				glm::vec3(0, 0, translationShapeSize.y()),
				new btCylinderShape(translationShapeSize),
				m_TransformGizmoMatZID,
				&m_TranslationGizmoTag,
				m_TranslationGizmo
			},

			{
				"Translation gizmo plane yz",
				MESH_DIRECTORY "translation-gizmo-plane-yz.glb",
				glm::quat(glm::vec3(0, PI_DIV_TWO, 0)),
				glm::vec3(0, translationPlaneOffset, translationPlaneOffset),
				new btBoxShape(translationPlaneShapeSize),
				m_TransformGizmoMatYZID,
				&m_TranslationGizmoTag,
				m_TranslationGizmoPlanes
			},
			{
				"Translation gizmo plane xz",
				MESH_DIRECTORY "translation-gizmo-plane-xz.glb",
				glm::quat(glm::vec3(PI_DIV_TWO, 0, 0)),
				glm::vec3(translationPlaneOffset, 0, translationPlaneOffset),
				new btBoxShape(translationPlaneShapeSize),
				m_TransformGizmoMatXZID,
				&m_TranslationGizmoTag,
				m_TranslationGizmoPlanes
			},
			{
				"Translation gizmo plane xy",
				MESH_DIRECTORY "translation-gizmo-plane-xy.glb",
				glm::quat(glm::vec3(0, 0, 0)),
				glm::vec3(translationPlaneOffset, translationPlaneOffset, 0),
				new btBoxShape(translationPlaneShapeSize),
				m_TransformGizmoMatXYID,
				&m_TranslationGizmoTag,
				m_TranslationGizmoPlanes
			},

			{
				"Rotation gizmo x axis",
				MESH_DIRECTORY "rotation-gizmo-flat-x.glb",
				glm::quat(glm::vec3(0, 0, -PI_DIV_TWO)),
				glm::vec3(rotationShapeSize.y(), 0, 0),
				new btCylinderShape(rotationShapeSize),
				m_TransformGizmoMatXID,
				&m_RotationGizmoTag,
				m_RotationGizmo
			},
			{
				"Rotation gizmo y axis",
				MESH_DIRECTORY "rotation-gizmo-flat-y.glb",
				glm::quat(glm::vec3(0, 0, 0)),
				glm::vec3(0, rotationShapeSize.y(), 0),
				new btCylinderShape(rotationShapeSize),
				m_TransformGizmoMatYID,
				&m_RotationGizmoTag,
				m_RotationGizmo
			},
			{
				"Rotation gizmo z axis",
				MESH_DIRECTORY "rotation-gizmo-flat-z.glb",
				glm::quat(glm::vec3(PI_DIV_TWO, 0, 0)),
				glm::vec3(0, 0, rotationShapeSize.y()),
				new btCylinderShape(rotationShapeSize),
				m_TransformGizmoMatZID,
				&m_RotationGizmoTag,
				m_RotationGizmo
			},

			{
				"Scale gizmo x axis",
				MESH_DIRECTORY "scale-gizmo-x.glb",
				glm::quat(glm::vec3(0, 0, -PI_DIV_TWO)),
				glm::vec3(scaleShapeSize.y(), 0, 0),
				new btCylinderShape(scaleShapeSize),
				m_TransformGizmoMatXID,
				&m_ScaleGizmoTag,
				m_ScaleGizmo
			},
			{
				"Scale gizmo y axis",
				MESH_DIRECTORY "scale-gizmo-y.glb",
				glm::quat(glm::vec3(0, 0, 0)),
				glm::vec3(0, scaleShapeSize.y(), 0),
				new btCylinderShape(scaleShapeSize),
				m_TransformGizmoMatYID,
				&m_ScaleGizmoTag,
				m_ScaleGizmo
			},
			{
				"Scale gizmo x axis",
				MESH_DIRECTORY "scale-gizmo-z.glb",
				glm::quat(glm::vec3(PI_DIV_TWO, 0, 0)),
				glm::vec3(0, 0, scaleShapeSize.y()),
				new btCylinderShape(scaleShapeSize),
				m_TransformGizmoMatZID,
				&m_ScaleGizmoTag,
				m_ScaleGizmo
			},
			{
				"Scale gizmo all axes",
				MESH_DIRECTORY "scale-gizmo-all.glb",
				QUAT_IDENTITY,
				VEC3_ZERO,
				new btBoxShape(scaleAllShapeSize),
				m_TransformGizmoMatAllID,
				&m_ScaleGizmoTag,
				m_ScaleGizmo
			},
		};

		for (MeshCreateInfo& createInfo : translationCreateInfos)
		{
			EditorObject* editorObject = new EditorObject(createInfo.name);
			editorObject->AddTag(*createInfo.tag);

			editorObject->SetCollisionShape(createInfo.collisionShape);

			RigidBody* rigidBody = editorObject->SetRigidBody(new RigidBody(gizmoRBGroup, gizmoRBMask));
			rigidBody->SetMass(0.0f);
			rigidBody->SetKinematic(true);
			rigidBody->SetPhysicsFlags(gizmoRBFlags);

			Mesh* subMesh = editorObject->SetMesh(new Mesh(editorObject));
			meshCreateInfo.relativeFilePath = createInfo.meshName;
			meshCreateInfo.materialIDs = { createInfo.matID };
			RenderObjectCreateInfo renderObjCreateInfo = {};
			if (createInfo.parent == m_TranslationGizmoPlanes)
			{
				renderObjCreateInfo.cullFace = CullFace::NONE;
			}
			meshCreateInfo.optionalRenderObjectCreateInfo = &renderObjCreateInfo;

			subMesh->LoadFromFile(meshCreateInfo);

			editorObject->GetTransform()->SetLocalRotation(createInfo.orientation, false);
			editorObject->GetTransform()->SetLocalPosition(createInfo.offset, true);

			createInfo.parent->AddEditorChildImmediate(editorObject);
		}

		m_TranslationGizmo->AddEditorChildImmediate(m_TranslationGizmoPlanes);
		m_TransformGizmo->AddEditorChildImmediate(m_TranslationGizmo);
		m_TransformGizmo->AddEditorChildImmediate(m_RotationGizmo);
		m_TransformGizmo->AddEditorChildImmediate(m_ScaleGizmo);


		m_TransformGizmo->Initialize();
		m_TransformGizmo->PostInitialize();

		currentScene->AddEditorObjectImmediate(m_TransformGizmo);

		m_CurrentTransformGizmoState = TransformState::TRANSLATE;
		UpdateGizmoVisibility();
	}

	void Editor::FadeOutHeadOnGizmos()
	{
		Transform* gizmoTransform = m_TransformGizmo->GetTransform();
		glm::vec3 centerToCam = glm::normalize(g_CameraManager->CurrentCamera()->position - gizmoTransform->GetWorldPosition());

		Material* xMat = g_Renderer->GetMaterial(m_TransformGizmoMatXID);
		Material* yMat = g_Renderer->GetMaterial(m_TransformGizmoMatYID);
		Material* zMat = g_Renderer->GetMaterial(m_TransformGizmoMatZID);
		Material* yzMat = g_Renderer->GetMaterial(m_TransformGizmoMatYZID);
		Material* xzMat = g_Renderer->GetMaterial(m_TransformGizmoMatXZID);
		Material* xyMat = g_Renderer->GetMaterial(m_TransformGizmoMatXYID);

		real camViewXAlignment = glm::abs(glm::dot(centerToCam, gizmoTransform->GetRight()));
		real camViewYAlignment = glm::abs(glm::dot(centerToCam, gizmoTransform->GetUp()));
		real camViewZAlignment = glm::abs(glm::dot(centerToCam, gizmoTransform->GetForward()));

		auto CalcAlpha = [](real threshold, real camViewAlignment, real power, bool invert)
		{
			if (invert)
			{
				if (camViewAlignment <= threshold)
				{
					return Lerp(1.0f, 0.0f, glm::pow((threshold - camViewAlignment) / threshold, power));
				}
				else
				{
					return 1.0f;
				}
			}
			else
			{
				if (camViewAlignment >= threshold)
				{
					return Lerp(1.0f, 0.0f, glm::pow((camViewAlignment - threshold) / (1.0f - threshold), power));
				}
				else
				{
					return 1.0f;
				}
			}
		};

		if (m_CurrentTransformGizmoState == TransformState::ROTATE)
		{
			const real threshold = 0.1f;
			const real power = 0.05f;
			xMat->colourMultiplier.a = CalcAlpha(threshold, camViewXAlignment, power, true);
			yMat->colourMultiplier.a = CalcAlpha(threshold, camViewYAlignment, power, true);
			zMat->colourMultiplier.a = CalcAlpha(threshold, camViewZAlignment, power, true);
		}
		else
		{
			{
				const real threshold = 0.95f;
				const real power = 0.2f;
				xMat->colourMultiplier.a = CalcAlpha(threshold, camViewXAlignment, power, false);
				yMat->colourMultiplier.a = CalcAlpha(threshold, camViewYAlignment, power, false);
				zMat->colourMultiplier.a = CalcAlpha(threshold, camViewZAlignment, power, false);
			}

			{
				const real threshold = 0.1f;
				const real power = 0.1f;
				yzMat->colourMultiplier.a = CalcAlpha(threshold, camViewXAlignment, power, true);
				xzMat->colourMultiplier.a = CalcAlpha(threshold, camViewYAlignment, power, true);
				xyMat->colourMultiplier.a = CalcAlpha(threshold, camViewZAlignment, power, true);
			}
		}
	}

	btVector3 Editor::GetAxisColour(i32 axisIndex) const
	{
		return axisIndex == X_AXIS_IDX ? btVector3(1.0f, 0.0f, 0.0f) : axisIndex == Y_AXIS_IDX ? btVector3(0.0f, 1.0f, 0.0f) : btVector3(0.1f, 0.1f, 1.0f);
	}

	Editor::JitterDetector::JitterDetector() :
		m_PositionXHisto(m_HistoLength),
		m_PositionZHisto(m_HistoLength),
		m_VelocityXHisto(m_HistoLength),
		m_VelocityZHisto(m_HistoLength),
		m_CamXHisto(m_HistoLength),
		m_CamZHisto(m_HistoLength),
		m_CamPosDiffXHisto(m_HistoLength),
		m_CamPosDiffZHisto(m_HistoLength)
	{
		real posRange = 14.0f;
		m_PositionXHisto.overrideMin = -posRange;
		m_PositionXHisto.overrideMax = posRange;
		m_PositionZHisto.overrideMin = -posRange;
		m_PositionZHisto.overrideMax = posRange;

		m_CamXHisto.overrideMin = -posRange;
		m_CamXHisto.overrideMax = posRange;
		m_CamZHisto.overrideMin = -posRange;
		m_CamZHisto.overrideMax = posRange;

		real velRange = 15.0f;
		m_VelocityXHisto.overrideMin = -velRange;
		m_VelocityXHisto.overrideMax = velRange;
		m_VelocityZHisto.overrideMin = -velRange;
		m_VelocityZHisto.overrideMax = velRange;
	}

	void Editor::JitterDetector::FixedUpdate()
	{
		Player* player = g_SceneManager->CurrentScene()->GetPlayer(0);

		if (player == nullptr || player->GetRigidBody() == nullptr)
		{
			return;
		}

		glm::vec3 posWS = player->GetTransform()->GetWorldPosition();
		btVector3 velWS = player->GetRigidBody()->GetRigidBodyInternal()->getLinearVelocity();

		m_PositionXHisto.AddElement(posWS.x);
		m_PositionZHisto.AddElement(posWS.z);

		m_VelocityXHisto.overrideMin = glm::min(m_VelocityXHisto.overrideMin, glm::min(velWS.x(), velWS.z()));
		m_VelocityXHisto.overrideMax = glm::max(m_VelocityXHisto.overrideMax, glm::max(velWS.x(), velWS.z()));
		m_VelocityZHisto.overrideMin = m_VelocityXHisto.overrideMin;
		m_VelocityZHisto.overrideMax = m_VelocityXHisto.overrideMax;

		m_VelocityXHisto.AddElement(velWS.x());
		m_VelocityZHisto.AddElement(velWS.z());

		BaseCamera* cam = g_CameraManager->CurrentCamera();

		m_CamXHisto.AddElement(cam->position.x);
		m_CamZHisto.AddElement(cam->position.z);

		m_CamPosDiffXHisto.AddElement(posWS.x - cam->position.x);
		m_CamPosDiffZHisto.AddElement(posWS.z - cam->position.z);
	}

	void Editor::JitterDetector::DrawImGuiObjects()
	{
		bool* bWindowShowing = g_EngineInstance->GetUIWindowOpen(SID_PAIR("jitter detector"));
		if (*bWindowShowing)
		{
			if (ImGui::Begin("Jitter Detector", bWindowShowing))
			{
				ImGui::Text("Pos");
				m_PositionXHisto.DrawImGui();
				m_PositionZHisto.DrawImGui();

				ImGui::Text("Vel");
				m_VelocityXHisto.DrawImGui();
				m_VelocityZHisto.DrawImGui();

				ImGui::Text("Cam");
				m_CamXHisto.DrawImGui();
				m_CamZHisto.DrawImGui();

				real lowestMin = FLT_MAX;
				real highestMax = FLT_MIN;
				for (real element : m_CamPosDiffXHisto.data)
				{
					lowestMin = glm::min(lowestMin, element);
					highestMax = glm::max(highestMax, element);
				}

				ImGui::Text("Cam-pos diff [%.1f - %.1f]", lowestMin, highestMax);
				m_CamPosDiffXHisto.DrawImGui();
				m_CamPosDiffZHisto.DrawImGui();
			}
			ImGui::End();
		}
	}
} // namespace flex
