#pragma once

namespace flex
{
	class BezierCurve3D
	{
	public:
		BezierCurve3D();
		BezierCurve3D(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3);

		void DrawDebug(bool bHighlighted, const btVector4& baseColour, const btVector4& highlightColour) const;

		glm::vec3 GetPointOnCurve(real t) const;
		// Returns non-normalized(!) first derivative of curve (representing direction at point)
		glm::vec3 GetFirstDerivativeOnCurve(real t) const;

		void CalculateLength();

		static BezierCurve3D FromString(const std::string& str);
		std::string ToString() const;

		glm::vec3 points[4];

		real calculatedLength = -1.0f;

		static const btVector4 s_PointColour;
		i32 debug_SegmentCount = 10;

	};
} // namespace flex