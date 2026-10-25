#pragma once

namespace flex
{
	// Lightweight CPU profiler used by PROFILE_* in non-debug builds (debug builds use palanteer). Times named scopes
	// on the main thread & reports per-frame averages. Disabled (costing a branch per scope) unless enabled at runtime.
	namespace Profiler
	{
		void SetEnabled(bool bEnabled);
		bool IsEnabled();

		void BeginScope(const char* name);
		void EndScope();
		// Called once per frame (on the main thread)
		void EndFrame();

		// Records a value measured elsewhere (e.g. GPU timings), averaged per frame in the report
		void AddSample(const char* name, ms duration);

		// Prints scopes sorted by average time per frame
		void PrintReport();
		void Reset();

		struct Scope
		{
			explicit Scope(const char* name) : bActive(IsEnabled())
			{
				if (bActive)
				{
					BeginScope(name);
				}
			}

			~Scope()
			{
				if (bActive)
				{
					EndScope();
				}
			}

			bool bActive;
		};
	} // namespace Profiler
} // namespace flex
