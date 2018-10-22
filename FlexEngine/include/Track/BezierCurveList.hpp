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

		// o---o----o---o|o---o----o---o|o---o----o---o
		// 0             1              2             3
		glm::vec3 GetPointAtJunction(i32 index);

		const std::vector<BezierCurve>& GetCurves() const;

	private:
		void GetCurveIndexAndDistAlongCurve(real t, i32& outIndex, real& outT) const;

		void DEBUG_GenerateRandomSeed();

		std::vector<BezierCurve> m_Curves;

		real m_DebugColourRandomSeed = -1.0f;
		btVector4 m_BaseColour;

	};
} // namespace flex