#pragma once

IGNORE_WARNINGS_PUSH
#include "LinearMath/btMotionState.h"
IGNORE_WARNINGS_POP

namespace flex
{
	ATTRIBUTE_ALIGNED16(struct)
		MotionState : public btMotionState
	{
		BT_DECLARE_ALIGNED_ALLOCATOR();

		MotionState(Transform* correspondingTransform);

		// Synchronizes world transform from user to physics
		virtual void getWorldTransform(btTransform& centerOfMassWorldTrans) const override;

		// Synchronizes world transform from physics to user
		// Bullet only calls the update of worldtransform for active objects
		virtual void setWorldTransform(const btTransform& centerOfMassWorldTrans);

		Transform* m_Transform = nullptr;
	};
} // namespace flex
