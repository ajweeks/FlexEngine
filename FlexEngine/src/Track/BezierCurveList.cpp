#include "stdafx.hpp"

#include "Track/BezierCurveList.hpp"

#include "Graphics/Renderer.hpp"

#pragma warning(push, 0)
#include "LinearMath/btIDebugDraw.h"
#pragma warning(pop)

namespace flex
{
	BezierCurveList::BezierCurveList()
	{
		DEBUG_GenerateRandomSeed();
	}

	BezierCurveList::BezierCurveList(const std::vector<BezierCurve>& curves) :
		m_Curves(curves)
	{
		DEBUG_GenerateRandomSeed();
	}

	void BezierCurveList::DrawDebug(const btVector4& highlightColour, real highlightCurveAtPoint /* = -1.0f */) const
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
			m_Curves[i].DrawDebug(bHighlighted, m_BaseColour, highlightColour);
		}
	}

	glm::vec3 BezierCurveList::GetPointOnCurve(real t, i32& outCurveIndex) const
	{
		if (m_Curves.empty())
		{
			return VEC3_ZERO;
		}

		real curveT = 0.0f;
		GetCurveIndexAndDistAlongCurve(t, outCurveIndex, curveT);

		return m_Curves[outCurveIndex].GetPointOnCurve(curveT);
	}

	glm::vec3 BezierCurveList::GetCurveDirectionAt(real t) const
	{
		if (m_Curves.empty())
		{
			return VEC3_ZERO;
		}

		i32 curveIndex = 0;
		real curveT = 0.0f;
		GetCurveIndexAndDistAlongCurve(t, curveIndex, curveT);

		return m_Curves[curveIndex].GetCurveDirectionAt(curveT);
	}

	glm::vec3 BezierCurveList::GetPointAtJunction(i32 index)
	{
		real t = 0.0f;
		if (index == m_Curves.size())
		{
			t = 1.0f;
		}

		return m_Curves[glm::clamp(index, 0, (i32)(m_Curves.size() - 1))].GetPointOnCurve(t);
	}

	const std::vector<BezierCurve>& BezierCurveList::GetCurves() const
	{
		return m_Curves;
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

	void BezierCurveList::DEBUG_GenerateRandomSeed()
	{
		m_DebugColourRandomSeed = (real)rand() / (real)RAND_MAX;

		m_BaseColour = btVector4(
			0.75f + 0.3f * fmod(15.648f * m_DebugColourRandomSeed, 1.0f),
			0.2f + 0.4f * fmod(0.342f + 6.898f * m_DebugColourRandomSeed, 1.0f),
			0.2f + 0.4f * fmod(0.158f + 2.221f * m_DebugColourRandomSeed, 1.0f),
			1.0f);

	}
} // namespace flex
