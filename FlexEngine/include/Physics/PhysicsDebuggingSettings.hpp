#pragma once

namespace flex
{
	struct PhysicsDebuggingSettings
	{
		bool bDisableAll = false;

		bool bDrawWireframe = false;
		bool bDrawAabb = false;
		bool bDrawFeaturesText = false;
		bool bDrawContactPoints = false;
		bool bNoDeactivation = false;
		bool bNoHelpText = false;
		bool bDrawText = false;
		bool bProfileTimings = false;
		bool bEnableSatComparison = false;
		bool bDisableBulletLCP = false;
		bool bEnableCCD = false;
		bool bDrawConstraints = false;
		bool bDrawConstraintLimits = false;
		bool bFastWireframe = false;
		bool bDrawNormals = false;
		bool bDrawFrames = false;

		// Only objects (& triangles) within this distance of the camera are drawn
		real drawRadius = 60.0f;
	};
} // namespace flex
