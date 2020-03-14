#include "stdafx.hpp"

#include "Track/BezierCurveList.hpp"

#include "Graphics/Renderer.hpp"

IGNORE_WARNINGS_PUSH
#include "LinearMath/btIDebugDraw.h"
IGNORE_WARNINGS_POP

#include "Track/BezierCurve.hpp"

namespace flex
{
	BezierCurveList::BezierCurveList()
	{
		DEBUG_GenerateRandomSeed();
	}

	BezierCurveList::BezierCurveList(const std::vector<BezierCurve>& curves) :
		curves(curves)
	{
		DEBUG_GenerateRandomSeed();
	}

	BezierCurveList::~BezierCurveList()
	{
		btAlignedFree(m_BaseColour);
		m_BaseColour = nullptr;
	}

	void BezierCurveList::InitializeFromJSON(const JSONObject& obj)
	{
		curves.clear();

		for (const JSONField& field : obj.fields)
		{
			std::string curveString = field.value.strValue;
			curves.emplace_back(BezierCurve::FromString(curveString));
		}
	}

	void BezierCurveList::DrawDebug(const btVector4& highlightColour, real highlightCurveAtPoint /* = -1.0f */) const
	{
		i32 highlightedCurveIndex = -1;
		if (highlightCurveAtPoint != -1.0f)
		{
			real dist;
			GetCurveIndexAndLocalTFromGlobalT(highlightCurveAtPoint, &highlightedCurveIndex, &dist);
		}

		for (i32 i = 0; i < (i32)curves.size(); ++i)
		{
			bool bHighlighted = (highlightedCurveIndex == i);
			curves[i].DrawDebug(bHighlighted, *m_BaseColour, highlightColour);
		}
	}

	glm::vec3 BezierCurveList::GetPointOnCurve(real t, i32* outCurveIndex) const
	{
		if (curves.empty())
		{
			return VEC3_ZERO;
		}

		real curveT = 0.0f;
		GetCurveIndexAndLocalTFromGlobalT(t, outCurveIndex, &curveT);

		return curves[*outCurveIndex].GetPointOnCurve(curveT);
	}

	glm::vec3 BezierCurveList::GetPointOnCurve(i32 curveIndex, i32 pointIndex) const
	{
		return curves[curveIndex].points[pointIndex];
	}

	void BezierCurveList::SetPointPosAtIndex(i32 curveIndex, i32 pointIndex, const glm::vec3& point, bool bKeepHandlesMirrored)
	{
		glm::vec3 dPos = point - curves[curveIndex].points[pointIndex];
		curves[curveIndex].points[pointIndex] = point;
		if (pointIndex == 0 && curveIndex > 0)
		{
			curves[curveIndex - 1].points[3] = point;
		}
		else if (pointIndex == 3 && curveIndex < (i32)curves.size() - 1)
		{
			curves[curveIndex + 1].points[0] = point;
		}

		if (bKeepHandlesMirrored)
		{
			if (pointIndex == 2 && curveIndex < (i32)curves.size() - 1)
			{
				curves[curveIndex + 1].points[1] = curves[curveIndex].points[3] + (curves[curveIndex].points[3] - curves[curveIndex].points[2]);
			}
			else if (pointIndex == 1 && curveIndex > 0)
			{
				curves[curveIndex - 1].points[2] = curves[curveIndex].points[0] + (curves[curveIndex].points[0] - curves[curveIndex].points[1]);
			}
			else if (pointIndex == 0)
			{
				curves[curveIndex].points[1] += dPos;

				if (curveIndex > 0)
				{
					curves[curveIndex - 1].points[2] += dPos;
				}
			}
			else if (pointIndex == 3)
			{
				curves[curveIndex].points[2] += dPos;

				if (curveIndex < (i32)curves.size() - 1)
				{
					curves[curveIndex + 1].points[1] += dPos;
				}
			}
		}
	}

	glm::vec3 BezierCurveList::GetCurveDirectionAt(real t) const
	{
		if (curves.empty())
		{
			return VEC3_ZERO;
		}

		i32 curveIndex = 0;
		real curveT = 0.0f;
		GetCurveIndexAndLocalTFromGlobalT(t, &curveIndex, &curveT);

		return glm::normalize(curves[curveIndex].GetFirstDerivativeOnCurve(curveT));
	}

	real BezierCurveList::GetTAtJunction(i32 curveIndex)
	{
		return (real)curveIndex / (real)(curves.size());
	}

	glm::vec3 BezierCurveList::GetPointAtJunction(i32 index)
	{
		real t = (index == (i32)curves.size() ? 1.0f : 0.0f);
		return curves[glm::clamp(index, 0, (i32)(curves.size() - 1))].GetPointOnCurve(t);
	}

	void BezierCurveList::GetCurveIndexAndLocalTFromGlobalT(real globalT, i32* outCurveIndex, real* outLocalT) const
	{
		i32 curveCount = (i32)curves.size();
		real scaledT = glm::clamp(globalT * curveCount, 0.0f, (real)curveCount - EPSILON);
		*outCurveIndex = glm::clamp((i32)scaledT, 0, curveCount - 1);

		*outLocalT = scaledT - *outCurveIndex;
		*outLocalT = glm::clamp(*outLocalT, 0.0f, 1.0f);
	}

	real BezierCurveList::GetGlobalTFromCurveIndexAndLocalT(i32 curveIndex, real localT) const
	{
		i32 curveCount = (i32)curves.size();
		real globalT = (real)(localT + curveIndex) / (real)curveCount;

		globalT = glm::clamp(globalT, 0.0f, 1.0f);
		return globalT;
	}

	bool BezierCurveList::IsVectorFacingDownTrack(real distAlongTrack, const glm::vec3& vec)
	{
		// TODO: Make this return the intuitive result (the opposite of this)
		real dotResult = glm::dot(GetCurveDirectionAt(distAlongTrack), vec);
		return (dotResult <= 0.001f);
	}

	JSONObject BezierCurveList::Serialize() const
	{
		JSONObject result = {};

		for (const BezierCurve& curve : curves)
		{
			result.fields.emplace_back("curve", JSONValue(curve.ToString()));
		}

		return result;
	}

	void BezierCurveList::DEBUG_GenerateRandomSeed()
	{
		m_DebugColourRandomSeed = (real)rand() / (real)RAND_MAX;

		btAlignedFree(m_BaseColour);
		void* mem = btAlignedAlloc(sizeof(btVector4), 16);
		m_BaseColour = new (mem) btVector4(
			0.75f + 0.3f * fmod(15.648f * m_DebugColourRandomSeed, 1.0f),
			0.2f + 0.4f * fmod(0.342f + 6.898f * m_DebugColourRandomSeed, 1.0f),
			0.2f + 0.4f * fmod(0.158f + 2.221f * m_DebugColourRandomSeed, 1.0f),
			1.0f);

	}
} // namespace flex
