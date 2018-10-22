#pragma once

#include "Track/BezierCurve.hpp"

namespace flex
{
	class BezierCurveList
	{
	public:
		BezierCurveList();
		BezierCurveList(const std::vector<BezierCurve>& curves);

		void DrawDebug(const btVector3& highlightColour, real highlightCurveAtPoint = -1.0f) const;

		glm::vec3 GetPointOnCurve(real t) const;
		glm::vec3 GetFirstDerivative(real t) const;

	private:
		void GetCurveIndexAndDistAlongCurve(real t, i32& outIndex, real& outT) const;

		std::vector<BezierCurve> m_Curves;

	};
} // namespace flex