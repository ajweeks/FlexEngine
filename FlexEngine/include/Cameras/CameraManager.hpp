#pragma once

#include <vector>

namespace flex
{
	class BaseCamera;
	struct GameContext;
	
	class CameraManager final
	{
	public:
		CameraManager();
		~CameraManager();

		void Initialize(const GameContext& gameContext);
		void Update(const GameContext& gameContext);

		void DestroyCameras();

		BaseCamera* CurrentCamera() const;
		i32 CameraCount() const;

		void AddCamera(BaseCamera* camera, bool switchTo = false);

		/*
		 * Sets active camera index to camera's index, if found, otherwise does nothing
		 * Optionally aligns position, rotation, and FOV to current camera
		 */
		void SwtichTo(const GameContext& gameContext, BaseCamera* camera, bool align = true);

		/*
		* Sets active camera index to index (if index is in valid range)
		* Optionally aligns position, rotation, and FOV to current camera
		*/
		void SwtichToIndex(const GameContext& gameContext, i32 index, bool align = true);

		/*
		 * Adds delta to active camera index (can be negative)
		 * Optionally aligns position, rotation, and FOV to current camera
		 */
		void SwtichToIndexRelative(const GameContext& gameContext, i32 delta, bool align = true);

	private:
		i32 GetCameraIndex(BaseCamera* camera);

		/* Copies position, rotation, and FOV of "from" to "to" (unchecked!) */
		void AlignCameras(BaseCamera* from, BaseCamera* to);

		std::vector<BaseCamera*> m_Cameras;
		i32 m_ActiveCameraIndex = -1;

		// TODO: Implement camera animation here
		//bool animating = false;

	};
} // namespace flex
