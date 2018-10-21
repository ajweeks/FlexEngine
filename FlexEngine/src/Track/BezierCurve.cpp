#include "stdafx.hpp"

#include "Track/BezierCurve.hpp"

#include "Graphics/Renderer.hpp"

#pragma warning(push, 0)
#include "LinearMath/btIDebugDraw.h"
#pragma warning(pop)

namespace flex
{
	BezierCurve::BezierCurve()
	{
	}

	BezierCurve::BezierCurve(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3)
	{
		m_Points[0] = p0;
		m_Points[1] = p1;
		m_Points[2] = p2;
		m_Points[3] = p3;
	}

	void BezierCurve::DrawDebug(bool bHighlighted, const btVector3& highlightColour) const
	{
		btIDebugDraw* debugDrawer = g_Renderer->GetDebugDrawer();

		btVector3 lineColour = bHighlighted ? highlightColour : btVector3(0.9f, 0.2f, 0.9f);
		i32 segmentCount = 20;
		btVector3 pPoint = ToBtVec3(m_Points[0]);
		for (i32 i = 0; i <= segmentCount; ++i)
		{
			real t = (real)i / (real)segmentCount;
			btVector3 nPoint = ToBtVec3(GetPointOnCurve(t));

#define DRAW_LOCAL_AXES 0
#if DRAW_LOCAL_AXES
			glm::vec3 railForward = glm::normalize(GetFirstDerivative(t));
			glm::vec3 railRight = glm::cross(railForward, glm::vec3(0.0f, 1.0f, 0.0f));
			glm::vec3 railUp = glm::cross(railRight, railForward);
			debugDrawer->drawLine(nPoint, nPoint + ToBtVec3(railRight), btVector3(0.9f, 0.1f, 0.1f));
			debugDrawer->drawLine(nPoint, nPoint + ToBtVec3(railForward), btVector3(0.1f, 0.5f, 0.9f));
			debugDrawer->drawLine(nPoint, nPoint + ToBtVec3(railUp), btVector3(0.1f, 0.9f, 0.1f));
#else
			debugDrawer->drawLine(pPoint, nPoint, lineColour);
#endif

			pPoint = nPoint;
		}

		btVector3 pointColour(0.2f, 0.2f, 0.1f);
		for (const glm::vec3& point : m_Points)
		{
			debugDrawer->drawSphere(ToBtVec3(point), 0.1f, pointColour);
		}
	}

	glm::vec3 BezierCurve::GetPointOnCurve(real t) const
	{
		t = glm::clamp(t, 0.0f, 1.0f);
		real oneMinusT = 1.0f - t;
		return oneMinusT * oneMinusT * oneMinusT * m_Points[0] +
			3.0f * oneMinusT * oneMinusT * t * m_Points[1] +
			3.0f * oneMinusT * t * t * m_Points[2] +
			t * t * t * m_Points[3];
	}

	glm::vec3 BezierCurve::GetFirstDerivative(real t) const
	{
		t = glm::clamp(t, 0.0f, 1.0f);
		real oneMinusT = 1.0f - t;
		return 3.0f * oneMinusT * oneMinusT * (m_Points[1] - m_Points[0]) +
			6.0f * oneMinusT * t * (m_Points[2] - m_Points[1]) +
			3.0f * t * t * (m_Points[3] - m_Points[2]);
	}
} // namespace flex
