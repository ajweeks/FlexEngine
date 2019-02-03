#pragma once

#include <vector>
#include <stack>

#include "Callbacks/InputCallbacks.hpp"

namespace flex
{
	class BaseCamera;

	class CameraManager final
	{
	public:
		CameraManager();

		void Initialize();
		void Destroy();
		void Update();

		void OnSceneChanged();

		BaseCamera* CurrentCamera() const;
		i32 CameraCount() const;

		void AddCamera(BaseCamera* camera, bool bSwitchTo = false);

		// Clears stack and pushes the given camera onto it
		void SetCamera(BaseCamera* camera, bool bAlignWithPrevious);
		void CycleCamera(i32 deltaIndex, bool bAlignWithPrevious = true);
		void SetCameraByName(const std::string& name, bool bAlignWithPrevious);

		void PushCamera(BaseCamera* camera, bool bAlignWithPrevious);
		void PushCameraByName(const std::string& name, bool bAlignWithPrevious);
		void PopCamera();

		void DrawImGuiObjects();

	private:
		EventReply OnActionEvent(Action action);
		ActionCallback<CameraManager> m_ActionCallback;

		BaseCamera* GetCameraByName(const std::string& name);
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
