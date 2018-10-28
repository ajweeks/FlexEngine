#pragma once

namespace flex
{
	class BezierCurve
	{
	public:
		BezierCurve();
		BezierCurve(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3);

		void DrawDebug(bool bHighlighted, const btVector4& baseColour, const btVector4& highlightColour) const;

		glm::vec3 GetPointOnCurve(real t) const;
		// Returns non-normalized(!) first derivative of curve (representing direction at point)
		glm::vec3 GetCurveDirectionAt(real t) const;

		glm::vec3 points[4];

	};
} // namespace flex