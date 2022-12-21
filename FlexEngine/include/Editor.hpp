#pragma once

#include "Histogram.hpp"
#include "InputTypes.hpp"
#include "Callbacks/InputCallbacks.hpp"

namespace flex
{
	class EditorObject;

	class Editor
	{
	public:
		Editor();

		void Initialize();
		void PostInitialize();
		void Destroy();
		void EarlyUpdate();
		void LateUpdate();
		void FixedUpdate();
		void PreSceneChange();
		void OnSceneChanged();
		void DrawImGuiObjects();

		std::vector<GameObjectID> GetSelectedObjectIDs(bool bForceIncludeChildren = false) const;
		GameObjectID GetFirstSelectedObjectID() const;
		void SetSelectedObject(const GameObjectID& gameObjectID, bool bSelectChildren = false);
		void SetSelectedObjects(const std::vector<GameObjectID>& selectedObjects);
		bool HasSelectedObject() const;
		bool HasSelectedEditorObject() const;
		void ToggleSelectedObject(const GameObjectID& gameObjectID);
		void AddSelectedObject(const GameObjectID& gameObjectID);
		void SelectAll();
		void DeselectObject(const GameObjectID& gameObjectID);
		bool IsObjectSelected(const GameObjectID& gameObjectID);
		glm::vec3 GetSelectedObjectsCenter();
		void SelectNone();

		void SetSelectedEditorObject(EditorObjectID* editorObjectID);
		bool IsEditorObjectSelected(EditorObjectID* editorObjectID);
		EditorObjectID GetSelectedEditorObject() const;

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
		static const char* PrefabPayloadCStr;
		static const char* GameObjectPayloadCStr;
		static const char* AudioFileNameSIDPayloadCStr;

	private:
		EventReply OnMouseButtonEvent(MouseButton button, KeyAction action);
		MouseButtonCallback<Editor> m_MouseButtonCallback;

		EventReply OnMouseMovedEvent(const glm::vec2& dMousePos);
		MouseMovedCallback<Editor> m_MouseMovedCallback;

		EventReply OnKeyEvent(KeyCode keyCode, KeyAction action, i32 modifiers);
		KeyEventCallback<Editor> m_KeyEventCallback;

		EventReply OnActionEvent(Action action, ActionEvent actionEvent);
		ActionCallback<Editor> m_ActionCallback;

		void CreateObjects();
		void FadeOutHeadOnGizmos();

		btVector3 GetAxisColour(i32 axisIndex) const;

		// Parent of translation, rotation, and scale gizmo objects
		EditorObject* m_TransformGizmo = nullptr;
		// Children of m_TransformGizmo
		EditorObject* m_TranslationGizmo = nullptr;
		EditorObject* m_RotationGizmo = nullptr;
		EditorObject* m_ScaleGizmo = nullptr;

		EditorObject* m_GridObject = nullptr;

		MaterialID m_TransformGizmoMatXID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatYID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatZID = InvalidMaterialID;
		MaterialID m_TransformGizmoMatAllID = InvalidMaterialID;

		TransformState m_CurrentTransformGizmoState = TransformState::TRANSLATE;

		const std::string m_TranslationGizmoTag = "translation-gizmo";
		const std::string m_RotationGizmoTag = "rotation-gizmo";
		const std::string m_ScaleGizmoTag = "scale-gizmo";

		glm::vec2i m_LMBDownPos;

		glm::vec3 m_SelectedObjectDragStartPos;
		glm::vec3 m_DraggingGizmoScaleLast1;
		glm::vec3 m_DraggingGizmoScaleLast2;
		real m_DraggingGizmoOffset = 0.0f; // How far along the axis the cursor was when pressed
		glm::vec3 m_PreviousIntersectionPoint;
		bool m_DraggingGizmoOffsetNeedsRecalculation = true;
		bool m_bFirstFrameDraggingRotationGizmo = false;
		glm::vec3 m_AxisProjectedOnto;
		glm::vec3 m_StartPointOnPlane;
		i32 m_RotationGizmoWrapCount = 0;
		real m_LastAngle = -1.0f;
		glm::vec3 m_AxisOfRotation;
		bool m_bLastDotPos = false;
		real m_AngleSnap;

		bool m_bShowGrid = false;

		// TODO: EZ: Define these in config file
		real m_ScaleDragSpeed = 0.05f;
		real m_ScaleSlowDragSpeedMultiplier = 0.2f;
		real m_ScaleFastDragSpeedMultiplier = 2.5f;

		bool m_bDraggingGizmo = false;

		i32 m_DraggingAxisIndex = -1;
		i32 m_HoveringAxisIndex = -1;

		std::vector<GameObjectID> m_CurrentlySelectedObjectIDs;
		EditorObjectID m_CurrentlySelectedEditorObjectID = InvalidEditorObjectID;

		glm::vec3 m_SelectedObjectsCenterPos;
		glm::quat m_SelectedObjectRotation;

		bool m_bWantRenameActiveElement = false;

		struct JitterDetector
		{
			JitterDetector();

			void FixedUpdate();
			void DrawImGuiObjects();

			u32 m_HistoLength = 128;
			Histogram m_PositionXHisto;
			Histogram m_PositionZHisto;
			Histogram m_VelocityXHisto;
			Histogram m_VelocityZHisto;
			Histogram m_CamXHisto;
			Histogram m_CamZHisto;
			Histogram m_CamPosDiffXHisto;
			Histogram m_CamPosDiffZHisto;

		};

		JitterDetector m_JitterDetector;
	};
} // namespace flex
