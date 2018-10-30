#pragma once

#include "Track/BezierCurve.hpp"

namespace flex
{
	class BezierCurveList
	{
	public:
		BezierCurveList();
		BezierCurveList(const std::vector<BezierCurve>& curves);

		void DrawDebug(const btVector4& highlightColour, real highlightCurveAtPoint = -1.0f) const;

		glm::vec3 GetPointOnCurve(real t, i32& outCurveIndex) const;
		// Calculates the first derivative of the curve
		glm::vec3 GetCurveDirectionAt(real t) const;

		real GetTAtJunction(i32 curveIndex);

		// o---o----o---o|o---o----o---o|o---o----o---o
		// 0             1              2             3
		glm::vec3 GetPointAtJunction(i32 index);
		glm::vec3 GetDirectionAtJunction(i32 index);

		void GetCurveIndexAndLocalTFromGlobalT(real globalT, i32& outCurveIndex, real& outLocalT) const;
		real GetGlobalTFromCurveIndexAndLocalT(i32 curveIndex, real localT) const;

		std::vector<BezierCurve> curves;

	private:
		void DEBUG_GenerateRandomSeed();

		real m_DebugColourRandomSeed = -1.0f;
		btVector4 m_BaseColour;

	};
} // namespace flex