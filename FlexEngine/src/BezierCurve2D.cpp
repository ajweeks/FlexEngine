#include "stdafx.hpp"

#include "BezierCurve2D.hpp"

#include "Graphics/Renderer.hpp"
#include "Helpers.hpp"

namespace flex
{
	BezierCurve2D::BezierCurve2D()
	{
		points[0] = glm::vec2(0.25f, 0.25f);
		points[1] = glm::vec2(0.75f, 0.75f);
	}

	BezierCurve2D::BezierCurve2D(const glm::vec2& p0, const glm::vec2& p1)
	{
		points[0] = p0;
		points[1] = p1;
	}

	void BezierCurve2D::DrawImGui()
	{
		ImGui::Bezier("bezier", &(points[0].x), false);
	}

	glm::vec2 BezierCurve2D::GetPointOnCurve(real t)
	{
		t = Saturate(t);
		// TODO: Investigate perf difference with using ImGui::BezierValue
		//return ImGui::BezierValue(t, &(points[0].x));
		real oneMinusT = 1.0f - t;
		return 3.0f * oneMinusT * oneMinusT * t * points[0] +
			3.0f * oneMinusT * t * t * points[1] +
			glm::vec2(t * t * t);
	}

	BezierCurve2D BezierCurve2D::FromString(const std::string& str)
	{
		assert(!str.empty());

		BezierCurve2D result = {};

		std::vector<std::string> parts = Split(str, ',');
		if (parts.size() != 12)
		{
			PrintWarn("Invalidly formatted BezierCurve string! %s\n", str.c_str());
		}

		result.points[0] = glm::vec2(std::atoi(parts[0].c_str()), std::atoi(parts[1].c_str()));
		result.points[1] = glm::vec2(std::atoi(parts[2].c_str()), std::atoi(parts[3].c_str()));

		return result;
	}

	std::string BezierCurve2D::ToString() const
	{
		const i32 precision = 5;
		const char* delim = ", ";
		std::string result(VecToString(points[0], precision) + delim +
			VecToString(points[1], precision));

		return result;
	}
} // namespace flex
