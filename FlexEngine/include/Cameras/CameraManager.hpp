#pragma once

#include <vector>
#include <stack>

#include "Callbacks/InputCallbacks.hpp"

namespace flex
{
	class BaseCamera;

	// TODO: Make inherit from System
	class CameraManager final
	{
	public:
		CameraManager();

		void Initialize();
		void Destroy();
		void Update();
		void OnPostSceneChange();

		BaseCamera* CurrentCamera() const;

		void AddCamera(BaseCamera* camera, bool bSwitchTo = false);

		// Clears stack and pushes the given camera onto it, then returns a pointer to it
		BaseCamera* SetCamera(BaseCamera* camera, bool bAlignWithPrevious);
		BaseCamera* CycleCamera(i32 deltaIndex, bool bAlignWithPrevious = true);
		BaseCamera* SetCameraByName(const std::string& name, bool bAlignWithPrevious);

		BaseCamera* PushCamera(BaseCamera* camera, bool bAlignWithPrevious, bool bInitialize);
		BaseCamera* PushCameraByName(const std::string& name, bool bAlignWithPrevious, bool bInitialize);
		void PopCamera();

		BaseCamera* GetCameraByName(const std::string& name);

		void DrawImGuiObjects();

	private:
		EventReply OnActionEvent(Action action, ActionEvent actionEvent);
		ActionCallback<CameraManager> m_ActionCallback;

		i32 GetCameraIndex(BaseCamera* camera);

		/* Copies position, rotation, and FOV of "from" to "to" */
		void AlignCameras(BaseCamera* from, BaseCamera* to);

		// TODO: Roll custom stack class
		// Stack containing temporary cameras, the topmost of which is the current camera
		// Always contains at least one element
		std::stack<BaseCamera*> m_CameraStack;
		// All cameras, unordered
		std::vector<BaseCamera*> m_Cameras;

		bool m_bInitialized = false;

	};
} // namespace flex
