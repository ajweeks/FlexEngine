#pragma once

IGNORE_WARNINGS_PUSH
#include "LinearMath/btIDebugDraw.h"
IGNORE_WARNINGS_POP

#include "Helpers.hpp"

namespace flex
{
	class DebugRenderer : public btIDebugDraw
	{
	public:
		virtual ~DebugRenderer() {};

		virtual void Initialize() = 0;
		virtual void PostInitialize() = 0;
		virtual void Destroy() = 0;
		virtual void DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& colour) = 0;
		virtual void DrawLineWithAlpha(const btVector3& from, const btVector3& to, const btVector4& colourFrom, const btVector4& colourTo) = 0;
		virtual void DrawBasis(const btVector3& origin, const btVector3& right, const btVector3& up, const btVector3& forward) = 0;

		virtual void OnPreSceneChange() = 0;
		virtual void OnPostSceneChange() = 0;

		virtual void flushLines() override;

		void DrawAxes(const btVector3& origin, const btQuaternion& orientation, real scale);

		void UpdateDebugMode();
		void ClearLines();

	protected:
		virtual void Draw() = 0;

		struct LineSegment
		{
			LineSegment() = default;

			LineSegment(const btVector3& vStart, const btVector3& vEnd, const btVector3& vColFrom, const btVector3& vColTo)
			{
				memcpy(start, vStart.m_floats, sizeof(real) * 3);
				memcpy(end, vEnd.m_floats, sizeof(real) * 3);
				memcpy(colourFrom, vColFrom.m_floats, sizeof(real) * 3);
				memcpy(colourTo, vColTo.m_floats, sizeof(real) * 3);
				colourFrom[3] = 1.0f;
				colourTo[3] = 1.0f;
			}
			LineSegment(const btVector3& vStart, const btVector3& vEnd, const btVector4& vColFrom, const btVector3& vColTo)
			{
				memcpy(start, vStart.m_floats, sizeof(real) * 3);
				memcpy(end, vEnd.m_floats, sizeof(real) * 3);
				memcpy(colourFrom, vColFrom.m_floats, sizeof(real) * 4);
				memcpy(colourTo, vColTo.m_floats, sizeof(real) * 4);
			}
			real start[3];
			real end[3];
			real colourFrom[4];
			real colourTo[4];
		};

		// TODO: Allocate from pool to reduce startup alloc size (currently 57MB!)
		static const u32 MAX_NUM_LINE_SEGMENTS = 1'048'576;
		u32 m_LineSegmentIndex = 0;
		LineSegment m_LineSegments[MAX_NUM_LINE_SEGMENTS];

		i32 m_DebugMode = 0;

	};
} // namespace flex
