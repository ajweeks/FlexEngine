#pragma once

namespace flex
{
	class BezierCurve2D
	{
	public:
		BezierCurve2D();
		BezierCurve2D(const glm::vec2& p0, const glm::vec2& p1);

		void DrawImGui();

		glm::vec2 GetPointOnCurve(real t);

		static BezierCurve2D FromString(const std::string& str);
		std::string ToString() const;

		// Points 0 & 3 are assumed to be (0, 0) & (1, 1)
		glm::vec2 points[2];

		real calculatedLength = -1.0f;

	};
} // namespace flex