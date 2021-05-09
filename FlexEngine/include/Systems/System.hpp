#pragma once

namespace flex
{
	enum class SystemType
	{
		PLUGGABLES,
		ROAD_MANAGER,
		TERMINAL_MANAGER,
		TRACK_MANAGER,
		CART_MANAGER,

		_NONE
	};

	class System
	{
	public:
		virtual void Initialize() = 0;
		virtual void Destroy() = 0;
		virtual void Update() = 0;

		virtual void DrawImGui();

	};
} // namespace flex
