#pragma once

IGNORE_WARNINGS_PUSH
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
IGNORE_WARNINGS_POP

namespace flex
{
	// Exposes the world's actions (e.g. vehicles), which bullet keeps protected
	class DynamicsWorld final : public btDiscreteDynamicsWorld
	{
	public:
		using btDiscreteDynamicsWorld::btDiscreteDynamicsWorld;

		i32 GetActionCount() const { return m_actions.size(); }
		btActionInterface* GetAction(i32 index) { return m_actions[index]; }
	};
} // namespace flex
