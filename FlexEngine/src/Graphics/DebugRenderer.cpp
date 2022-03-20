
#include "stdafx.hpp"

#include "Graphics/DebugRenderer.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/quaternion.hpp> // for rotate
IGNORE_WARNINGS_POP

#include "Graphics/Renderer.hpp"
#include "Physics/PhysicsDebuggingSettings.hpp"

namespace flex
{
	void DebugRenderer::flushLines()
	{
		Draw();
	}

	void DebugRenderer::DrawAxes(const btVector3& origin, const btQuaternion& orientation, real scale)
	{
		drawLine(origin, origin + quatRotate(orientation, btVector3(scale, 0.0f, 0.0f)), btVector3(0.9f, 0.1f, 0.1f));
		drawLine(origin, origin + quatRotate(orientation, btVector3(0.0f, scale, 0.0f)), btVector3(0.1f, 0.9f, 0.1f));
		drawLine(origin, origin + quatRotate(orientation, btVector3(0.0f, 0.0f, scale)), btVector3(0.1f, 0.1f, 0.9f));
	}

	void DebugRenderer::UpdateDebugMode()
	{
		const PhysicsDebuggingSettings& settings = g_Renderer->GetPhysicsDebuggingSettings();

		m_DebugMode =
			(settings.bDisableAll ? DBG_NoDebug : 0) |
			(settings.bDrawWireframe ? DBG_DrawWireframe : 0) |
			(settings.bDrawAabb ? DBG_DrawAabb : 0) |
			(settings.bDrawFeaturesText ? DBG_DrawFeaturesText : 0) |
			(settings.bDrawContactPoints ? DBG_DrawContactPoints : 0) |
			(settings.bNoDeactivation ? DBG_NoDeactivation : 0) |
			(settings.bNoHelpText ? DBG_NoHelpText : 0) |
			(settings.bDrawText ? DBG_DrawText : 0) |
			(settings.bProfileTimings ? DBG_ProfileTimings : 0) |
			(settings.bEnableSatComparison ? DBG_EnableSatComparison : 0) |
			(settings.bDisableBulletLCP ? DBG_DisableBulletLCP : 0) |
			(settings.bEnableCCD ? DBG_EnableCCD : 0) |
			(settings.bDrawConstraints ? DBG_DrawConstraints : 0) |
			(settings.bDrawConstraintLimits ? DBG_DrawConstraintLimits : 0) |
			(settings.bFastWireframe ? DBG_FastWireframe : 0) |
			(settings.bDrawNormals ? DBG_DrawNormals : 0) |
			(settings.bDrawFrames ? DBG_DrawFrames : 0);
	}

	void DebugRenderer::ClearLines()
	{
		m_LineSegmentIndex = 0;
	}
} // namespace flex
