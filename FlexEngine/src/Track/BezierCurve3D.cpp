#include "stdafx.hpp"

#include "Track/BezierCurve3D.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/norm.hpp>
IGNORE_WARNINGS_POP

#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"

namespace flex
{
	const btVector4 BezierCurve3D::s_PointColour = btVector4(0.2f, 0.2f, 0.1f, 0.25f);

	BezierCurve3D::BezierCurve3D()
	{
	}

	BezierCurve3D::BezierCurve3D(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3)
	{
		points[0] = p0;
		points[1] = p1;
		points[2] = p2;
		points[3] = p3;

		CalculateLength();
	}

	void BezierCurve3D::DrawDebug(bool bHighlighted, const btVector4& baseColour, const btVector4& highlightColour) const
	{
		PhysicsDebugDrawBase* debugDrawer = g_Renderer->GetDebugDrawer();

		btVector4 lineColour = bHighlighted ? highlightColour : baseColour;
		btVector3 pPoint = ToBtVec3(points[0]);
		for (i32 i = 1; i <= debug_SegmentCount; ++i)
		{
			real t = (real)i / (real)debug_SegmentCount;
			btVector3 nPoint = ToBtVec3(GetPointOnCurve(t));

			debugDrawer->DrawLineWithAlpha(pPoint, nPoint, lineColour);

			pPoint = nPoint;
		}

#define DRAW_HANDLES 1
#if DRAW_HANDLES
		debugDrawer->DrawLineWithAlpha(ToBtVec3(points[0]), ToBtVec3(points[1]), s_PointColour);
		debugDrawer->DrawLineWithAlpha(ToBtVec3(points[2]), ToBtVec3(points[3]), s_PointColour);
#endif

		btVector3 pointColours[] = { btVector3(0.8f, 0.1f, 0.1f), btVector3(0.1f, 0.8f, 0.1f), btVector3(0.1f, 0.1f, 0.8f), btVector3(0.8f, 0.8f, 0.8f) };
		for (u32 i = 0; i < 4; ++i)
		{
			const glm::vec3& point = points[i];
			debugDrawer->drawSphere(ToBtVec3(point), 0.1f + i * 0.02f, pointColours[i]);
		}
	}

	glm::vec3 BezierCurve3D::GetPointOnCurve(real t) const
	{
		t = Saturate(t);
		real oneMinusT = 1.0f - t;
		return oneMinusT * oneMinusT * oneMinusT * points[0] +
			3.0f * oneMinusT * oneMinusT * t * points[1] +
			3.0f * oneMinusT * t * t * points[2] +
			t * t * t * points[3];
	}

	glm::vec3 BezierCurve3D::GetPointOnCurveUnclamped(real t) const
	{
		real oneMinusT = 1.0f - t;
		return oneMinusT * oneMinusT * oneMinusT * points[0] +
			3.0f * oneMinusT * oneMinusT * t * points[1] +
			3.0f * oneMinusT * t * t * points[2] +
			t * t * t * points[3];
	}

	glm::vec3 BezierCurve3D::GetFirstDerivativeOnCurve(real t) const
	{
		t = Saturate(t);
		real oneMinusT = 1.0f - t;
		return 3.0f * oneMinusT * oneMinusT * (points[1] - points[0]) +
			6.0f * oneMinusT * t * (points[2] - points[1]) +
			3.0f * t * t * (points[3] - points[2]);
	}

	void BezierCurve3D::CalculateLength()
	{
		calculatedLength = 0.0f;

		i32 iterationCount = 128;
		glm::vec3 lastP = GetPointOnCurve(0.0f);
		for (i32 i = 1; i < iterationCount; ++i)
		{
			real t = (real)i / (real)(iterationCount - 1);
			glm::vec3 nextP = GetPointOnCurve(t);

			calculatedLength += glm::length(nextP - lastP);

			lastP = nextP;
		}
	}

	BezierCurve3D BezierCurve3D::FromString(const std::string& str)
	{
		assert(!str.empty());

		BezierCurve3D result = {};

		std::vector<std::string> parts = Split(str, ',');
		if (parts.size() != 12)
		{
			PrintWarn("Invalidly formatted BezierCurve string! %s\n", str.c_str());
		}

		result.points[0] = glm::vec3(std::atoi(parts[0].c_str()), std::atoi(parts[1].c_str()), std::atoi(parts[2].c_str()));
		result.points[1] = glm::vec3(std::atoi(parts[3].c_str()), std::atoi(parts[4].c_str()), std::atoi(parts[5].c_str()));
		result.points[2] = glm::vec3(std::atoi(parts[6].c_str()), std::atoi(parts[7].c_str()), std::atoi(parts[8].c_str()));
		result.points[3] = glm::vec3(std::atoi(parts[9].c_str()), std::atoi(parts[10].c_str()), std::atoi(parts[11].c_str()));

		result.CalculateLength();

		return result;
	}

	std::string BezierCurve3D::ToString() const
	{
		const i32 precision = 5;
		const char* delim = ", ";
		std::string result(VecToString(points[0], precision) + delim +
			VecToString(points[1], precision) + delim +
			VecToString(points[2], precision) + delim +
			VecToString(points[3], precision));

		return result;
	}

	real BezierCurve3D::FindDistanceAlong(const glm::vec3& point)
	{
		i32 sampleCount = 20;
		real shortestDist2 = 9999.0f;
		real tAtShortestDist = -1.0f;
		for (i32 i = 0; i < sampleCount; ++i)
		{
			real t = ((real)i - 1.0f) / sampleCount;
			real dist2 = glm::distance2(point, GetPointOnCurve(t));
			if (dist2 < shortestDist2)
			{
				shortestDist2 = dist2;
				tAtShortestDist = t;
			}
		}

		return tAtShortestDist;
	}
} // namespace flex
