#pragma once

#include <vector>

namespace flex
{
	class BaseCamera;

	class CameraManager final
	{
	public:
		CameraManager();
		~CameraManager();

		void Initialize();
		void Update();

		void OnSceneChanged();

		void DestroyCameras();

		BaseCamera* CurrentCamera() const;
		i32 CameraCount() const;

		void AddCamera(BaseCamera* camera, bool bSwitchTo = false);

		/*
		 * Sets active camera index to camera's index, if found, otherwise does nothing
		 * Optionally aligns position, rotation, and FOV to current camera
		 */
		void SwtichTo(BaseCamera* camera, bool bAlign = true);

		/*
		* Sets active camera index to index (if index is in valid range)
		* Optionally aligns position, rotation, and FOV to current camera
		*/
		void SwtichToIndex(i32 index, bool bAlign = true);

		/*
		 * Adds delta to active camera index (can be negative)
		 * Optionally aligns position, rotation, and FOV to current camera
		 */
		void SetActiveIndexRelative(i32 delta, bool bAlign = true);

		void SetActiveCameraByType(const std::string& typeStr);

	private:
		i32 GetCameraIndex(BaseCamera* camera);

		/* Copies position, rotation, and FOV of "from" to "to" (unchecked!) */
		void AlignCameras(BaseCamera* from, BaseCamera* to);

		std::vector<BaseCamera*> m_Cameras;
		i32 m_ActiveCameraIndex = -1;

	};
} // namespace flex
