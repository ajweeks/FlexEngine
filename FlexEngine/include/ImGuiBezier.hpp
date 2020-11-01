#pragma once

// ImGui Bezier widget. @r-lyeh, public domain
// v1.03: improve grabbing, confine grabbers to area option, adaptive size, presets, preview.
// v1.02: add BezierValue(); comments; usage
// v1.01: out-of-bounds coord snapping; custom border width; spacing; cosmetics
// v1.00: initial version
//
// [ref] http://robnapier.net/faster-bezier
// [ref] http://easings.net/es#easeInSine
//
// Usage:
// {  static float v[5] = { 0.390f, 0.575f, 0.565f, 1.000f };
//    ImGui::Bezier( "easeOutSine", v );       // draw
//    float y = ImGui::BezierValue( 0.5f, v ); // x delta in [0..1] range
// }

namespace ImGui
{
	template<flex::i32 steps>
	void bezier_table(glm::vec2 P[4], glm::vec2 results[steps + 1])
	{
		static flex::real C[(steps + 1) * 4], * K = 0;
		if (!K)
		{
			K = C;
			for (flex::u32 step = 0; step <= steps; ++step)
			{
				flex::real t = (flex::real)step / (flex::real)steps;
				C[step * 4 + 0] = (1 - t) * (1 - t) * (1 - t);   // * P0
				C[step * 4 + 1] = 3 * (1 - t) * (1 - t) * t;     // * P1
				C[step * 4 + 2] = 3 * (1 - t) * t * t;           // * P2
				C[step * 4 + 3] = t * t * t;                     // * P3
			}
		}
		for (flex::u32 step = 0; step <= steps; ++step)
		{
			glm::vec2 point = {
				K[step * 4 + 0] * P[0].x + K[step * 4 + 1] * P[1].x + K[step * 4 + 2] * P[2].x + K[step * 4 + 3] * P[3].x,
				K[step * 4 + 0] * P[0].y + K[step * 4 + 1] * P[1].y + K[step * 4 + 2] * P[2].y + K[step * 4 + 3] * P[3].y
			};
			results[step] = point;
		}
	}

	glm::vec2 BezierValue(flex::real dt01, flex::real* P);

	flex::i32 Bezier(const char* label, flex::real* P, bool bConstrainHandles);

	void ShowBezierDemo();
}