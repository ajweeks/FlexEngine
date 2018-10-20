#pragma once

#include <map>

#pragma warning(push, 0)
#include <glm/vec2.hpp>
#pragma warning(pop)

namespace flex
{
	class BezierCurve
	{
	public:
		BezierCurve();
		BezierCurve(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3);

		void DrawDebug(bool bHighlighted) const;

		glm::vec3 GetPointOnCurve(real t) const;
		glm::vec3 GetFirstDerivative(real t) const;

	private:
		glm::vec3 m_Points[4];

	};
} // namespace flex