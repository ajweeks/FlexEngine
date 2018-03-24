#pragma once

namespace flex
{
	struct PhysicsDebuggingSettings
	{
		bool DisableAll = false;

		bool DrawWireframe = false;
		bool DrawAabb = false;
		bool DrawFeaturesText = false;
		bool DrawContactPoints = false;
		bool NoDeactivation = false;
		bool NoHelpText = false;
		bool DrawText = false;
		bool ProfileTimings = false;
		bool EnableSatComparison = false;
		bool DisableBulletLCP = false;
		bool EnableCCD = false;
		bool DrawConstraints = false;
		bool DrawConstraintLimits = false;
		bool FastWireframe = false;
		bool DrawNormals = false;
		bool DrawFrames = false;
	};
} // namespace flex
