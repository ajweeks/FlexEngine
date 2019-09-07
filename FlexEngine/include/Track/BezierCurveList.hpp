#pragma once

#include "JSONTypes.hpp"

namespace flex
{
	class BezierCurve;

	class BezierCurveList
	{
	public:
		BezierCurveList();
		explicit BezierCurveList(const std::vector<BezierCurve>& curves);
		~BezierCurveList();

		static BezierCurveList InitializeFromJSON(const JSONObject& obj);

		void DrawDebug(const btVector4& highlightColour, real highlightCurveAtPoint = -1.0f) const;

		glm::vec3 GetPointOnCurve(real t, i32* outCurveIndex) const;
		glm::vec3 GetPointOnCurve(i32 curveIndex, i32 pointIndex) const;
		void SetPointPosAtIndex(i32 curveIndex, i32 pointIndex, const glm::vec3& point, bool bKeepHandlesMirrored);

		// Calculates the first derivative of the curve
		glm::vec3 GetCurveDirectionAt(real t) const;

		real GetTAtJunction(i32 curveIndex);

		// o---o----o---o|o---o----o---o|o---o----o---o
		// 0             1              2             3
		glm::vec3 GetPointAtJunction(i32 index);
		//glm::vec3 GetDirectionAtJunction(i32 index);

		void GetCurveIndexAndLocalTFromGlobalT(real globalT, i32* outCurveIndex, real* outLocalT) const;
		real GetGlobalTFromCurveIndexAndLocalT(i32 curveIndex, real localT) const;

		bool IsVectorFacingDownTrack(real distAlongTrack, const glm::vec3& vec);

		JSONObject Serialize() const;

		std::vector<BezierCurve> curves;

	private:
		void DEBUG_GenerateRandomSeed();

		real m_DebugColourRandomSeed = -1.0f;
		btVector4* m_BaseColour = nullptr;

	};
} // namespace flex