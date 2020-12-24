#pragma once

#include "InputTypes.hpp"
#include "Callbacks/InputCallbacks.hpp"

namespace flex
{
	class GameObject;

	class Editor
	{
	public:
		Editor();

		void Initialize();
		void PostInitialize();
		void Destroy();
		void EarlyUpdate();
		void LateUpdate();
		void PreSceneChange();
		void OnSceneChanged();

		std::vector<GameObject*> GetSelectedObjects(bool bForceIncludeChildren = false);
		void SetSelectedObject(GameObject* gameObject, bool bSelectChildren = false);
		void SetSelectedObjects(const std::vector<GameObject*>& selectedObjects);
		bool HasSelectedObject() const;
		void ToggleSelectedObject(GameObject* gameObject);
		void AddSelectedObject(GameObject* gameObject);
		void SelectAll();
		void DeselectObject(GameObject* gameObject);
		bool IsObjectSelected(GameObject* gameObject);
		glm::vec3 GetSelectedObjectsCenter();
		void SelectNone();

		real CalculateDeltaRotationFromGizmoDrag(
			const glm::vec3& axis,
			const glm::vec3& rayOrigin,
			const glm::vec3& rayEnd,
			glm::vec3* outIntersectionPoint);

		void UpdateGizmoVisibility();
		void SetTransformState(TransformState state);
		bool GetWantRenameActiveElement() const;
		void ClearWantRenameActiveElement();
		TransformState GetTransformState() const;
		void CalculateSelectedObjectsCenter();

		bool IsDraggingGizmo() const;
		bool HandleGizmoHover();
		void HandleGizmoClick();
		void HandleGizmoMovement();
		bool HandleObjectClick();

		void OnDragDrop(i32 count, const char** paths);

		bool IsShowingGrid() const;
		void SetShowGrid(bool bShowGrid);

		// ImGui payload identifiers
		static const char* MaterialPayloadCStr;
		static const char* MeshPayloadCStr;
		static const char* GameObjectPayloadCStr;

	private:
		EventReply OnMouseButtonEvent(MouseButton button, KeyAction action);
		MouseButtonCallback<Editor> m_MouseButtonCallback;

		EventReply OnMouseMovedEvent(const glm::vec2& dMousePos);
		MouseMovedCallback<Editor> m_MouseMovedCallback;

		EventReply OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers);
		KeyEventCallback<Editor> m_KeyEventCallback;

		EventReply OnActionEvent(Action action);
		ActionCallback<Editor> m_ActionCallback;

		void CreateObjects();
		void FadeOutHeadOnGizmos();

		btVector3 GetAxisColour(i32 axisIndex) const;

		// Parent of translation, rotation, and scale gizmo objects
		GameObject* m_TransformGizmo = nullptr;
		// Children of m_TransformGizmo
		GameObject* m_TranslationGizmo = nullptr;
		GameObject* m_RotationGizmo = nullptr;
		GameObject* m_ScaleGizmo = nullptr;

		GameObject* m_TestShape = nullptr;

		GameObject* m_GridObject = nullptr;

		MaterialID m_TransformGizmoMatXID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatYID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatZID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatAllID = InvalidMaterialID;

		TransformState m_CurrentTransformGizmoState = TransformState::TRANSLATE;

		const std::string m_TranslationGizmoTag = "translation-gizmo";
		const std::string m_RotationGizmoTag = "rotation-gizmo";
		const std::string m_ScaleGizmoTag = "scale-gizmo";

		// True for one frame after the mouse has been released after being pressed at the same location
		glm::vec2i m_LMBDownPos;

		glm::vec3 m_SelectedObjectDragStartPos;
		glm::quat m_SelectedObjectDragStartRot;
		glm::vec3 m_DraggingGizmoScaleLast;
		real m_DraggingGizmoOffset = 0.0f; // How far along the axis the cursor was when pressed
		glm::vec3 m_PreviousIntersectionPoint;
		bool m_DraggingGizmoOffsetNeedsRecalculation = true;
		bool m_bFirstFrameDraggingRotationGizmo = false;
		glm::vec3 m_AxisProjectedOnto;
		glm::vec3 m_StartPointOnPlane;
		glm::vec3 m_LatestRayPlaneIntersection;
		i32 m_RotationGizmoWrapCount = 0;
		real m_LastAngle = -1.0f;
		glm::vec3 m_PlaneN;
		glm::vec3 m_AxisOfRotation;
		bool m_bLastDotPos = false;

		bool m_bShowGrid = false;

		// TODO: EZ: Define these in config file
		real m_ScaleDragSpeed = 0.05f;
		real m_ScaleSlowDragSpeedMultiplier = 0.2f;
		real m_ScaleFastDragSpeedMultiplier = 2.5f;

		bool m_bDraggingGizmo = false;

		i32 m_DraggingAxisIndex = -1;
		i32 m_HoveringAxisIndex = -1;

		std::vector<GameObject*> m_CurrentlySelectedObjects;

		glm::vec3 m_SelectedObjectsCenterPos;
		glm::quat m_SelectedObjectRotation;

		bool m_bWantRenameActiveElement = false;

	};
} // namespace flex
