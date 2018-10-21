#include "stdafx.hpp"

#include "Track/BezierCurveList.hpp"

#include "Graphics/Renderer.hpp"
#include "Track/BezierCurve.hpp"

#pragma warning(push, 0)
#include "LinearMath/btIDebugDraw.h"
#pragma warning(pop)

namespace flex
{
	BezierCurveList::BezierCurveList()
	{
	}

	BezierCurveList::BezierCurveList(const std::vector<BezierCurve>& curves) :
		m_Curves(curves)
	{
	}

	void BezierCurveList::DrawDebug(const btVector3& highlightColour, real highlightCurveAtPoint /* = -1.0f */) const
	{
		i32 highlightedCurveIndex = -1;
		if (highlightCurveAtPoint != -1.0f)
		{
			real dist;
			GetCurveIndexAndDistAlongCurve(highlightCurveAtPoint, highlightedCurveIndex, dist);
		}

		for (i32 i = 0; i < m_Curves.size(); ++i)
		{
			bool bHighlighted = (highlightedCurveIndex == i);
			m_Curves[i].DrawDebug(bHighlighted, highlightColour);
		}
	}

	glm::vec3 BezierCurveList::GetPointOnCurve(real t) const
	{
		if (m_Curves.empty())
		{
			return glm::vec3(0.0f);
		}

		i32 curveIndex = 0;
		real curveT = 0.0f;
		GetCurveIndexAndDistAlongCurve(t, curveIndex, curveT);

		return m_Curves[curveIndex].GetPointOnCurve(curveT);
	}

	glm::vec3 BezierCurveList::GetFirstDerivative(real t) const
	{
		if (m_Curves.empty())
		{
			return glm::vec3(0.0f);
		}

		i32 curveIndex = 0;
		real curveT = 0.0f;
		GetCurveIndexAndDistAlongCurve(t, curveIndex, curveT);

		return m_Curves[curveIndex].GetFirstDerivative(curveT);
	}

	void BezierCurveList::GetCurveIndexAndDistAlongCurve(real t, i32& outIndex, real& outT) const
	{
		i32 curveCount = m_Curves.size();
		real tPerCurve = 1.0f / (real)curveCount;
		real scaledT = glm::clamp(t * curveCount, 0.0f, (real)curveCount - EPSILON);
		outIndex = glm::clamp((i32)scaledT, 0, curveCount - 1);

		outT = scaledT - outIndex;
		outT = glm::clamp(outT, 0.0f, 1.0f);
	}
} // namespace flex
