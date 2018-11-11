#include "stdafx.hpp"

#include "Track/BezierCurve.hpp"

#include "Graphics/Renderer.hpp"
#include "Graphics/GL/GLPhysicsDebugDraw.hpp"

namespace flex
{
	BezierCurve::BezierCurve()
	{
	}

	BezierCurve::BezierCurve(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3)
	{
		points[0] = p0;
		points[1] = p1;
		points[2] = p2;
		points[3] = p3;

		CalculateLength();
	}

	void BezierCurve::DrawDebug(bool bHighlighted, const btVector4& baseColour, const btVector4& highlightColour) const
	{
		gl::GLPhysicsDebugDraw* debugDrawer = (gl::GLPhysicsDebugDraw*)g_Renderer->GetDebugDrawer();

		btVector4 lineColour = bHighlighted ? highlightColour : baseColour;
		i32 segmentCount = 10;
		btVector3 pPoint = ToBtVec3(points[0]);
		for (i32 i = 0; i <= segmentCount; ++i)
		{
			real t = (real)i / (real)segmentCount;
			btVector3 nPoint = ToBtVec3(GetPointOnCurve(t));

#define DRAW_LOCAL_AXES 0
#if DRAW_LOCAL_AXES
			glm::vec3 trackForward = glm::normalize(GetCurveDirectionAt(t));
			glm::vec3 trackRight = glm::cross(trackForward, VEC3_UP);
			glm::vec3 trackUp = glm::cross(trackRight, trackForward);
			debugDrawer->drawLine(nPoint, nPoint + ToBtVec3(trackRight), btVector3(0.9f, 0.1f, 0.1f));
			debugDrawer->drawLine(nPoint, nPoint + ToBtVec3(trackForward), btVector3(0.1f, 0.5f, 0.9f));
			debugDrawer->drawLine(nPoint, nPoint + ToBtVec3(trackUp), btVector3(0.1f, 0.9f, 0.1f));
#else
			debugDrawer->DrawLineWithAlpha(pPoint, nPoint, lineColour);
#endif

			pPoint = nPoint;
		}

		btVector4 pointColour(0.2f, 0.2f, 0.1f, 0.25f);

#define DRAW_HANDLES 1
#if DRAW_HANDLES
		debugDrawer->DrawLineWithAlpha(ToBtVec3(points[0]), ToBtVec3(points[1]), pointColour);
		debugDrawer->DrawLineWithAlpha(ToBtVec3(points[2]), ToBtVec3(points[3]), pointColour);
#endif

		for (const glm::vec3& point : points)
		{
			debugDrawer->drawSphere(ToBtVec3(point), 0.1f, pointColour);
		}
	}

	glm::vec3 BezierCurve::GetPointOnCurve(real t) const
	{
		t = glm::clamp(t, 0.0f, 1.0f);
		real oneMinusT = 1.0f - t;
		return oneMinusT * oneMinusT * oneMinusT * points[0] +
			3.0f * oneMinusT * oneMinusT * t * points[1] +
			3.0f * oneMinusT * t * t * points[2] +
			t * t * t * points[3];
	}

	glm::vec3 BezierCurve::GetFirstDerivativeOnCurve(real t) const
	{
		t = glm::clamp(t, 0.0f, 1.0f);
		real oneMinusT = 1.0f - t;
		return 3.0f * oneMinusT * oneMinusT * (points[1] - points[0]) +
			6.0f * oneMinusT * t * (points[2] - points[1]) +
			3.0f * t * t * (points[3] - points[2]);
	}

	void BezierCurve::CalculateLength()
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
} // namespace flex
