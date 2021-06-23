#pragma once

#include "Helpers.hpp"
#include "JSONParser.hpp"
#include "Pair.hpp"
#include "PoolAllocator.hpp"

#include "VirtualMachine/Backend/VirtualMachine.hpp"
#include "VirtualMachine/Frontend/Parser.hpp"
#include "VirtualMachine/Frontend/Lexer.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/euler_angles.hpp>
IGNORE_WARNINGS_POP


namespace flex
{
	class FlexTest
	{
	private:
#define JSON_UNIT_TEST(FuncName) static bool FuncName() \
	{ \
		const char* FunctionName = #FuncName;

#define JSON_UNIT_TEST_END JSONParser::ClearErrors(); return true; }

#define UNIT_TEST(FuncName) static bool FuncName() \
	{ \
		const char* FunctionName = #FuncName;

#define UNIT_TEST_END return true; }

		static bool Expect(const char* funcName, int lineNumber, bool val, bool exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + (exp ? "true" : "false") + ", got " + (val ? "true" : "false") + ", error message:\n\t" + msg;
				PrintError("%s\n", msgStr.c_str());
				return false;
			}
			return true;
		}

		static bool Expect(const char* funcName, int lineNumber, glm::vec3 val, glm::vec3 exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + VecToString(exp) + ", got " + VecToString(val) + ", error message:\n\t" + msg;
				PrintError("%s\n", msgStr.c_str());
				return false;
			}
			return true;
		}

		static bool Expect(const char* funcName, int lineNumber, u32 val, u32 exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + std::to_string(exp) + ", got " + std::to_string(val) + ", error message:\n\t" + msg;
				PrintError("%s\n", msgStr.c_str());
				return false;
			}
			return true;
		}

		static bool Expect(const char* funcName, int lineNumber, u64 val, u64 exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + std::to_string(exp) + ", got " + std::to_string(val) + ", error message:\n\t" + msg;
				PrintError("%s\n", msgStr.c_str());
				return false;
			}
			return true;
		}

		static bool Expect(const char* funcName, int lineNumber, i32 val, i32 exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + std::to_string(exp) + ", got " + std::to_string(val) + ", error message:\n\t" + msg;
				PrintError("%s\n", msgStr.c_str());
				return false;
			}
			return true;
		}

		static bool Expect(const char* funcName, int lineNumber, i64 val, i64 exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + std::to_string(exp) + ", got " + std::to_string(val) + ", error message:\n\t" + msg;
				PrintError("%s\n", msgStr.c_str());
				return false;
			}
			return true;
		}

		static bool Expect(const char* funcName, int lineNumber, real val, real exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + std::to_string(exp) + ", got " + std::to_string(val) + ", error message:\n\t" + msg;
				PrintError("%s\n", msgStr.c_str());
				return false;
			}
			return true;
		}

		static bool Expect(const char* funcName, int lineNumber, const char* val, const char* exp, const char* msg)
		{
			if (strcmp(val, exp) != 0)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + std::string(exp) + ", got " + std::string(val) + ", error message:\n\t" + std::string(msg);
				PrintError("%s\n", msgStr.c_str());
				return false;
			}
			return true;
		}

		static bool Expect(const char* funcName, int lineNumber, JSONValue::Type val, JSONValue::Type exp, const char* msg)
		{
			return Expect(funcName, lineNumber, (u32)val, (u32)exp, msg);
		}

		//
		// JSON tests
		//

#define EXPECT(val, exp) if (!Expect(FunctionName, __LINE__, val, exp, JSONParser::GetErrorString())) { return false; }

		JSON_UNIT_TEST(EmptyFileIsParsed)
		{
			std::string jsonStr = R"()";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(MinimalFileIsParsed)
		{
			std::string jsonStr = R"({ })";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(OneFieldFileIsValid)
		{
			std::string jsonStr = R"({ "label" : "strValue" })";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
			EXPECT((u64)jsonObj.fields.size(), (u64)1u);
			EXPECT(jsonObj.fields[0].label.c_str(), "label");
			EXPECT(jsonObj.fields[0].value.type, JSONValue::Type::STRING);
			EXPECT(jsonObj.fields[0].value.strValue.c_str(), "strValue");
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(MissingQuoteFailsToParse)
		{
			std::string jsonStr = R"({ "label" : "strValue })";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, false);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(ObjectParsedCorrectly)
		{
			std::string jsonStr = R"(
			{
				"label" :
				{
					"sublabel" : "childValue1"
				}
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
			EXPECT((u64)jsonObj.fields.size(), (u64)1u);
			EXPECT(jsonObj.fields[0].label.c_str(), "label");
			EXPECT(jsonObj.fields[0].value.type, JSONValue::Type::OBJECT);
			EXPECT((u64)jsonObj.fields[0].value.objectValue.fields.size(), (u64)1u);
			EXPECT(jsonObj.fields[0].value.objectValue.fields[0].label.c_str(), "sublabel");
			EXPECT(jsonObj.fields[0].value.objectValue.fields[0].value.type, JSONValue::Type::STRING);
			EXPECT(jsonObj.fields[0].value.objectValue.fields[0].value.strValue.c_str(), "childValue1");
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(MissingSquareBracketFailsToParse)
		{
			std::string jsonStr = R"(
			{
				"label" :
				[
					"array elem 0"
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, false);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(MissingCurlyBracketFailsToParse)
		{
			std::string jsonStr = R"(
			{
				"label" :
				[
					"array elem 0"
				]
			)";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, false);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(LineCommentIgnored)
		{
			std::string jsonStr = R"(
			{
				// "ignored label" : "ignored field",
				"label" :
				[
					"array elem 0"
				]
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
			EXPECT((u64)jsonObj.fields.size(), (u64)1u);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(MultipleFieldsParsedCorrectly)
		{
			std::string jsonStr = R"(
			{
				"label 0" : "value 0",
				"label 1" : "value 1"
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
			EXPECT((u64)jsonObj.fields.size(), (u64)2u);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(JSONArray0)
		{
			std::string jsonStr = R"(
			{
				"fields" : [ "pristine", "pumpernickel", "packaging", "pasta" ]
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
			const JSONField& arrayField = jsonObj.fields[0];
			const std::vector<JSONField>& fields = arrayField.value.fieldArrayValue;
			EXPECT(arrayField.value.type, JSONValue::Type::FIELD_ARRAY);
			EXPECT((u64)fields.size(), (u64)4u);
			EXPECT(fields[0].value.type, JSONValue::Type::STRING);
			EXPECT(fields[0].value.strValue.c_str(), "pristine");
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(JSONArray1)
		{
			std::string jsonStr = R"(
			{
				"fields" :
				[
					12345,
					6789,
					9999,
					-10,
					0
				]
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
			const JSONField& arrayField = jsonObj.fields[0];
			const std::vector<JSONField>& fields = arrayField.value.fieldArrayValue;
			EXPECT(arrayField.value.type, JSONValue::Type::FIELD_ARRAY);
			EXPECT((u64)fields.size(), (u64)5u);
			// Smallest possible integer type that can store all values
			EXPECT(fields[0].value.type, JSONValue::Type::INT);
			EXPECT(fields[0].value.AsInt(), 12345);
			EXPECT(fields[1].value.AsInt(), 6789);
			EXPECT(fields[2].value.AsInt(), 9999);
			EXPECT(fields[3].value.AsInt(), -10);
			EXPECT(fields[4].value.AsInt(), 0);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(JSONArray2)
		{
			std::string jsonStr = R"(
			{
				"fields" :
				[
					2147483649, // Larger than INT_MAX - must be stored in 64 int
					6789,
					9999,
					-10,
					0
				]
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
			const JSONField& arrayField = jsonObj.fields[0];
			const std::vector<JSONField>& fields = arrayField.value.fieldArrayValue;
			EXPECT(arrayField.value.type, JSONValue::Type::FIELD_ARRAY);
			EXPECT((u64)fields.size(), (u64)5u);
			// Smallest possible integer type that can store all values
			EXPECT(fields[0].value.type, JSONValue::Type::LONG);
			EXPECT(fields[0].value.AsLong(), (i64)2147483649);
			EXPECT(fields[1].value.AsLong(), (i64)6789);
			EXPECT(fields[2].value.AsLong(), (i64)9999);
			EXPECT(fields[3].value.AsLong(), (i64)-10);
			EXPECT(fields[4].value.AsLong(), (i64)0);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(JSONArray3)
		{
			std::string jsonStr = R"(
			{
				"fields" :
				[
					-9223372036854775808, // i64 min
					18446744073709551615  // u64 max
				]
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			// Failure due to no possible integer type that can store both fields
			EXPECT(bSuccess, false);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(ArrayParsedCorrectly)
		{
			std::string jsonStr = R"(
			{
				"label" :
				[
					"array elem 0"
				]
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
			EXPECT((u64)jsonObj.fields.size(), (u64)1u);
			EXPECT(jsonObj.fields[0].label.c_str(), "label");
			EXPECT(jsonObj.fields[0].value.type, JSONValue::Type::FIELD_ARRAY);
			EXPECT((u64)jsonObj.fields[0].value.fieldArrayValue.size(), (u64)1u);
			EXPECT(jsonObj.fields[0].value.fieldArrayValue[0].label.c_str(), "");
			EXPECT(jsonObj.fields[0].value.fieldArrayValue[0].value.strValue.c_str(), "array elem 0");
			EXPECT(jsonObj.fields[0].value.fieldArrayValue[0].value.type, JSONValue::Type::STRING);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(ArrayMissingCommaInArrayFailsParse)
		{
			std::string jsonStr = R"(
			{
				"fields" :
				[
					"label 0" : "value 0"
					"label 1" : "value 1"
				]
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, false);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(ComplexFileIsValid)
		{
			std::string jsonStr = R"(
			{
				"version" : 3,
				"name" : "scene_prop_test",
				"spawn player" : false,
				"objects" :
				[
					{
						"name" : "Skybox",
						"type" : "skybox",
						"visible" : true,
						"transform" :
						{
						},
						"materials" :
						[
							"skybox 01"
						],
						"skybox info" :
						{
						}
					},
					{
						"name" : "Directional Light",
						"type" : "directional light",
						"visible" : true,
						"transform" :
						{
							"pos" : "0.000, 15.000, 0.000",
							"rot" : "-0.001, 0.365, 0.824"
						},
						"directional light info" :
						{
							"rotation" : "-0.073, 0.166, 0.394, 0.901",
							"pos" : "0.000, 15.000, 0.000",
							"colour" : "1.00, 1.00, 1.00",
							"enabled" : true,
							"brightness" : 3.047000,
							"cast shadows" : true,
							"shadow darkness" : 1.000000
						}
					}
				]
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
			EXPECT((u64)jsonObj.fields.size(), (u64)4u);
			JSONField spawnPlayerField = jsonObj.fields[2];
			EXPECT(spawnPlayerField.value.type, JSONValue::Type::BOOL);
			EXPECT(spawnPlayerField.value.boolValue, false);
			JSONField objectArrayField = jsonObj.fields[3];
			EXPECT(objectArrayField.value.type, JSONValue::Type::OBJECT_ARRAY);
			EXPECT((u64)objectArrayField.value.objectArrayValue.size(), (u64)2u); // 2 objects

			const JSONObject& skyboxObj = objectArrayField.value.objectArrayValue[0];
			EXPECT((u64)skyboxObj.fields.size(), (u64)6u);
			const JSONField& materialsField = skyboxObj.fields[4];
			EXPECT(materialsField.value.type, JSONValue::Type::FIELD_ARRAY); // skybox materials array
			EXPECT((u64)materialsField.value.fieldArrayValue.size(), (u64)1u); // 1 material in materials array

			const JSONObject& directionalLightObj = objectArrayField.value.objectArrayValue[1];
			const JSONField& directionalLightInfoField = directionalLightObj.fields[4];
			EXPECT(directionalLightInfoField.label.c_str(), "directional light info");
			const JSONField& brightnessField = directionalLightInfoField.value.objectValue.fields[4];
			EXPECT(brightnessField.value.type, JSONValue::Type::FLOAT);
			EXPECT(brightnessField.value.floatValue, 3.047f);
		}
		JSON_UNIT_TEST_END;

#undef EXPECT

		//
		// Math tests
		//

#define EXPECT(val, exp) if (!Expect(FunctionName, __LINE__, val, exp, "")) { return false; }

		UNIT_TEST(RayPlaneIntersectionOriginValid)
		{
			glm::vec3 axis(1.0f, 0.0f, 0.0f);
			glm::vec3 rayOrigin(0.0f, 0.0f, -1.0f);
			glm::vec3 rayEnd(0.0f, 0.0f, 1.0f);
			glm::vec3 planeOrigin(0.0f, 0.0f, 0.0f);
			glm::vec3 planeNorm(0.0f, 0.0f, -1.0f);
			glm::vec3 startPos(0.0f, 0.0f, 0.0f);
			glm::vec3 camForward(0.0f, 0.0f, 1.0f);
			real offset = 0.0f;
			glm::vec3 z(VEC3_ZERO);
			glm::vec3 constrainedIntersection = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayOrigin, rayEnd, planeOrigin, planeNorm, startPos, camForward, offset, false, z);
			EXPECT(constrainedIntersection, glm::vec3(0.0f, 0.0f, 0.0f));
		}
		UNIT_TEST_END;

		UNIT_TEST(RayPlaneIntersectionXYValid)
		{
			glm::vec3 axis(1.0f, 0.0f, 0.0f);
			glm::vec3 rayOrigin(1.0f, 1.0f, -1.0f);
			glm::vec3 rayEnd(1.0f, 1.0f, 1.0f);
			glm::vec3 planeOrigin(0.0f, 0.0f, 0.0f);
			glm::vec3 planeNorm(0.0f, 0.0f, -1.0f);
			glm::vec3 startPos(0.0f, 0.0f, 0.0f);
			glm::vec3 camForward(0.0f, 0.0f, 1.0f);
			real offset = 0.0f;
			glm::vec3 z(VEC3_ZERO);
			glm::vec3 constrainedIntersection = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayOrigin, rayEnd, planeOrigin, planeNorm, startPos, camForward, offset, false, z);
			EXPECT(constrainedIntersection, glm::vec3(1.0f, 0.0f, 0.0f)); // intersection point should be (1, 1, 0), constrained to x axis: (1, 0, 0)
		}
		UNIT_TEST_END;

		UNIT_TEST(RayPlaneIntersectionXY2Valid)
		{
			glm::vec3 axis(0.0f, 1.0f, 0.0f);
			glm::vec3 rayOrigin(-1.0f, 3.0f, -1.0f);
			glm::vec3 rayEnd(-1.0f, 3.0f, 1.0f);
			glm::vec3 planeOrigin(0.0f, 0.0f, 0.0f);
			glm::vec3 planeNorm(0.0f, 0.0f, -1.0f);
			glm::vec3 startPos(0.0f, 0.0f, 0.0f);
			glm::vec3 camForward(0.0f, 0.0f, 1.0f);
			real offset = 0.0f;
			glm::vec3 z(VEC3_ZERO);
			glm::vec3 constrainedIntersection = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayOrigin, rayEnd, planeOrigin, planeNorm, startPos, camForward, offset, false, z);
			EXPECT(constrainedIntersection, glm::vec3(0.0f, 3.0f, 0.0f)); // intersection point should be (-1, 3, 0), constrained to y axis: (0, 3, 0)
		}
		UNIT_TEST_END;

		UNIT_TEST(RayPlaneIntersectionXY3Valid)
		{
			glm::vec3 axis(0.0f, 0.0f, 1.0f);
			glm::vec3 rayOrigin(1.0f, -100.0f, 3.0f);
			glm::vec3 rayEnd(-1.0f, -100.0f, 3.0f);
			glm::vec3 planeOrigin(0.0f, 0.0f, 0.0f);
			glm::vec3 planeNorm(1.0f, 0.0f, 0.0f);
			glm::vec3 startPos(0.0f, 0.0f, 0.0f);
			glm::vec3 camForward(-1.0f, 0.0f, 0.0f);
			real offset = 0.0f;
			glm::vec3 z(VEC3_ZERO);
			glm::vec3 constrainedIntersection = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayOrigin, rayEnd, planeOrigin, planeNorm, startPos, camForward, offset, false, z);
			EXPECT(constrainedIntersection, glm::vec3(0.0f, 0.0f, 3.0f)); // intersection point should be (0, -100, 3), constrained to z axis: (0, 0, 3)
		}
		UNIT_TEST_END;

		UNIT_TEST(MinComponentValid)
		{
			glm::vec2 a(1.0f, 2.0);
			real result = MinComponent(a);
			EXPECT(result, 1.0f);

			glm::vec2 b(-9.0f, -12.0f);
			result = MinComponent(b);
			EXPECT(result, -12.0f);

			glm::vec2 c(-100506.008f, -100506.009f);
			result = MinComponent(c);
			EXPECT(result, -100506.009f);
		}
		UNIT_TEST_END;

		UNIT_TEST(MaxComponentValid)
		{
			glm::vec2 a(1.0f, 2.0f);
			real result = MaxComponent(a);
			EXPECT(result, 2.0f);

			glm::vec2 b(-9.0f, -12.0f);
			result = MaxComponent(b);
			EXPECT(result, -9.0f);

			glm::vec2 c(-100506.008f, -100506.009f);
			result = MaxComponent(c);
			EXPECT(result, -100506.008f);
		}
		UNIT_TEST_END;

		UNIT_TEST(QuaternionsAreNearlyEqual)
		{
			glm::quat a(glm::vec3(PI + EPSILON, PI / 2.0f, EPSILON));
			glm::quat b(glm::vec3(PI, PI / 2.0f - EPSILON, -EPSILON));
			bool result = NearlyEquals(a, b, 0.0001f);
			EXPECT(result, true);

			glm::quat c(glm::vec3((1.0f - PI) * 2.0f, -PI / 2.0f, 1.0f + EPSILON));
			glm::quat d(glm::vec3(2.0f - TWO_PI, -PI / 2.0f - EPSILON, 0.5 * 2.0f));
			result = NearlyEquals(c, d, 0.0001f);
			EXPECT(result, true);

			glm::quat e(-glm::sin(PI / 2.0f), glm::sin(PI / 4.0f), 0.0f, 0.0f);
			glm::quat f(-glm::sin(TWO_PI / 4.0f), glm::sin(PI / 4.0f), 0.0f, 0.0f);
			result = NearlyEquals(e, f, 0.0001f);
			EXPECT(result, true);
		}
		UNIT_TEST_END;

		UNIT_TEST(QuaternionsAreNotNearlyEqual)
		{
			glm::quat a(glm::vec3(TWO_PI, 2.0f, 0.0f));
			glm::quat b(glm::vec3(PI, PI / 2.0f, -EPSILON));
			bool result = NearlyEquals(a, b, 0.0001f);
			EXPECT(result, false);

			glm::quat c(glm::vec3((1.0f - PI) * 2.0f, -PI / 2.0f, 0.01f));
			glm::quat d(glm::vec3(2.0f - TWO_PI, -PI / 2.0f - EPSILON, -0.01f));
			result = NearlyEquals(c, d, 0.0001f);
			EXPECT(result, false);

			glm::quat e(-glm::sin(PI / 4.0f), glm::sin(PI / 4.0f), 0.0f, 0.0f);
			glm::quat f(glm::sin(-TWO_PI / 2.0f), glm::sin(PI / 4.0f), 0.0f, 1.0f);
			result = NearlyEquals(e, f, 0.0001f);
			EXPECT(result, false);

			glm::quat g(0.0f, 0.0f, 0.0f, 0.0f);
			glm::quat h(1.0f, 0.0f, 0.0f, 0.0f);
			glm::quat i(0.0f, 1.0f, 0.0f, 0.0f);
			glm::quat j(0.0f, 0.0f, 1.0f, 0.0f);
			glm::quat k(0.0f, 0.0f, 0.0f, 1.0f);
			result = NearlyEquals(g, h, 0.0001f);
			EXPECT(result, false);
			result = NearlyEquals(g, i, 0.0001f);
			EXPECT(result, false);
			result = NearlyEquals(g, j, 0.0001f);
			EXPECT(result, false);
			result = NearlyEquals(g, k, 0.0001f);
			EXPECT(result, false);

			glm::quat l(0.0f, 0.0f, 0.0f, 0.0001f);
			glm::quat m(0.0f, 0.0f, 0.0f, -0.0001f);
			result = NearlyEquals(l, m, 0.0001f);
			EXPECT(result, false);

			glm::quat n(0.0f, 0.0f, 0.0f, -0.0001f);
			glm::quat o(0.0f, 0.0f, 0.0f, 0.0001f);
			result = NearlyEquals(n, o, 0.0001f);
			EXPECT(result, false);
		}
		UNIT_TEST_END;

		//
		// Misc tests
		//

		UNIT_TEST(CountSetBitsValid)
		{
			EXPECT(CountSetBits(0b00000000), 0u);
			EXPECT(CountSetBits(0b00000001), 1u);
			EXPECT(CountSetBits(0b10000000), 1u);
			EXPECT(CountSetBits(0b01010101), 4u);
			EXPECT(CountSetBits(0b11111111), 8u);
			EXPECT(CountSetBits(0b1111111100000000), 8u);
			EXPECT(CountSetBits(0b1111111111111111), 16u);
		}
		UNIT_TEST_END;

		UNIT_TEST(PoolTests)
		{
			PoolAllocator<real, 4> pool;
			EXPECT((u64)pool.GetPoolSize(), (u64)4);
			EXPECT((u64)pool.GetPoolCount(), (u64)0);

			pool.Alloc();
			pool.Alloc();
			pool.Alloc();
			pool.Alloc();
			EXPECT((u64)pool.GetPoolCount(), (u64)1);
			EXPECT((u64)pool.MemoryUsed(), (u64)(sizeof(real) * 4));

			pool.Alloc();
			EXPECT((u64)pool.GetPoolCount(), (u64)2);
			EXPECT((u64)pool.MemoryUsed(), (u64)(sizeof(real) * 8));

			pool.ReleaseAll();
			EXPECT((u64)pool.GetPoolCount(), (u64)0);
			EXPECT((u64)pool.MemoryUsed(), (u64)0);
		}
		UNIT_TEST_END;

		UNIT_TEST(PairTests)
		{
			Pair<real, u32> p(1.0f, 23);

			EXPECT(p.first, 1.0f);
			EXPECT(p.second, (u32)23);

			p.first = 2.9f;
			p.second = 992;
			EXPECT(p.first, 2.9f);
			EXPECT(p.second, (u32)992);

			Pair<real, u32> p2 = p;
			EXPECT(p2.first, 2.9f);
			EXPECT(p2.second, (u32)992);

			Pair<real, u32> p3(p);
			EXPECT(p3.first, 2.9f);
			EXPECT(p3.second, (u32)992);

			Pair<real, u32> p4(std::move(p));
			EXPECT(p4.first, 2.9f);
			EXPECT(p4.second, (u32)992);

		}
		UNIT_TEST_END;

		//
		// Parse tests
		//

		UNIT_TEST(ParseTestBasic1)
		{
			AST::AST* ast = new AST::AST();

			const char* source = "int abcd = 1234;\n";
			ast->Generate(source);

			EXPECT(ast->diagnosticContainer->diagnostics.empty(), true);
			for (const Diagnostic& diagnostic : ast->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			std::string reconstructedStr = ast->rootBlock->ToString();
			EXPECT(strcmp(reconstructedStr.c_str(), source), 0);

			EXPECT((u64)ast->rootBlock->statements.size(), (u64)1);
			AST::Declaration* decl = dynamic_cast<AST::Declaration*>(ast->rootBlock->statements[0]);
			EXPECT(decl != nullptr, true);
			EXPECT((u32)decl->typeName, (u32)AST::TypeName::INT);
			EXPECT(strcmp(decl->identifierStr.c_str(), "abcd"), 0);
			AST::IntLiteral* intLit = dynamic_cast<AST::IntLiteral*>(decl->initializer);
			EXPECT(intLit != nullptr, true);
			EXPECT((u32)intLit->typeName, (u32)AST::TypeName::INT);
			EXPECT(intLit->value, 1234);

			ast->Destroy();
			delete ast;
		}
		UNIT_TEST_END;

		UNIT_TEST(ParseTestBasic2)
		{
			AST::AST* ast = new AST::AST();

			const char* source =
				"bool a = true;\n"
				"bool b = false;\n"
				"bool result = ((a && !b) && (b || a));\n";
			ast->Generate(source);

			EXPECT(ast->diagnosticContainer->diagnostics.empty(), true);
			for (const Diagnostic& diagnostic : ast->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			std::string reconstructedStr = ast->rootBlock->ToString();
			EXPECT(strcmp(reconstructedStr.c_str(), source), 0);

			EXPECT((u64)ast->rootBlock->statements.size(), (u64)3);

			AST::Declaration* decl = dynamic_cast<AST::Declaration*>(ast->rootBlock->statements[0]);
			EXPECT(decl != nullptr, true);
			EXPECT((u32)decl->typeName, (u32)AST::TypeName::BOOL);

			decl = dynamic_cast<AST::Declaration*>(ast->rootBlock->statements[1]);
			EXPECT(decl != nullptr, true);
			EXPECT((u32)decl->typeName, (u32)AST::TypeName::BOOL);

			decl = dynamic_cast<AST::Declaration*>(ast->rootBlock->statements[2]);
			EXPECT(decl != nullptr, true);
			EXPECT((u32)decl->typeName, (u32)AST::TypeName::BOOL);
			EXPECT(strcmp(decl->identifierStr.c_str(), "result"), 0);
			AST::BinaryOperation* bin0 = dynamic_cast<AST::BinaryOperation*>(decl->initializer);
			EXPECT(bin0 != nullptr, true);
			EXPECT((u32)bin0->operatorType, (u32)AST::BinaryOperatorType::BOOLEAN_AND);
			AST::BinaryOperation* lhs = dynamic_cast<AST::BinaryOperation*>(bin0->lhs);
			AST::BinaryOperation* rhs = dynamic_cast<AST::BinaryOperation*>(bin0->rhs);
			EXPECT(lhs != nullptr, true);
			EXPECT(rhs != nullptr, true);
			EXPECT((u32)lhs->operatorType, (u32)AST::BinaryOperatorType::BOOLEAN_AND);
			EXPECT((u32)rhs->operatorType, (u32)AST::BinaryOperatorType::BOOLEAN_OR);
			AST::Identifier* a0 = dynamic_cast<AST::Identifier*>(lhs->lhs);
			AST::UnaryOperation* b0Op = dynamic_cast<AST::UnaryOperation*>(lhs->rhs);
			EXPECT(b0Op != nullptr, true);
			EXPECT((u32)b0Op->operatorType, (u32)AST::UnaryOperatorType::NOT);
			AST::Identifier* b0 = dynamic_cast<AST::Identifier*>(b0Op->expression);
			AST::Identifier* b1 = dynamic_cast<AST::Identifier*>(rhs->lhs);
			AST::Identifier* a1 = dynamic_cast<AST::Identifier*>(rhs->rhs);
			EXPECT(a0 != nullptr, true);
			EXPECT(b0 != nullptr, true);
			EXPECT(b1 != nullptr, true);
			EXPECT(a1 != nullptr, true);
			EXPECT(strcmp(a0->identifierStr.c_str(), "a"), 0);
			EXPECT(strcmp(b0->identifierStr.c_str(), "b"), 0);
			EXPECT(strcmp(b1->identifierStr.c_str(), "b"), 0);
			EXPECT(strcmp(a1->identifierStr.c_str(), "a"), 0);

			ast->Destroy();
			delete ast;
		}
		UNIT_TEST_END;

		UNIT_TEST(ParseTestEmptyFor)
		{
			AST::AST* ast = new AST::AST();

			const char* source =
				"for (;;)\n{\n}\n";
			ast->Generate(source);

			EXPECT(ast->diagnosticContainer->diagnostics.empty(), true);
			for (const Diagnostic& diagnostic : ast->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			std::string reconstructedStr = ast->rootBlock->ToString();
			EXPECT(strcmp(reconstructedStr.c_str(), source), 0);

			EXPECT((u64)ast->rootBlock->statements.size(), (u64)1);

			AST::ForStatement* forStatement = dynamic_cast<AST::ForStatement*>(ast->rootBlock->statements[0]);
			EXPECT(forStatement != nullptr, true);
			EXPECT(forStatement->setup == nullptr, true);
			EXPECT(forStatement->condition == nullptr, true);
			EXPECT(forStatement->update == nullptr, true);
			EXPECT(forStatement->body != nullptr, true);
			AST::StatementBlock* body = dynamic_cast<AST::StatementBlock*>(forStatement->body);
			EXPECT(body != nullptr, true);
			EXPECT((u64)body->statements.size(), (u64)0);

			ast->Destroy();
			delete ast;
		}
		UNIT_TEST_END;

		UNIT_TEST(ParseTestEmptyWhile)
		{
			AST::AST* ast = new AST::AST();

			const char* source =
				"while (1)\n{\n}\n";
			ast->Generate(source);

			EXPECT(ast->diagnosticContainer->diagnostics.empty(), true);
			for (const Diagnostic& diagnostic : ast->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			std::string reconstructedStr = ast->rootBlock->ToString();
			EXPECT(strcmp(reconstructedStr.c_str(), source), 0);

			EXPECT((u64)ast->rootBlock->statements.size(), (u64)1);

			AST::WhileStatement* whileStatement = dynamic_cast<AST::WhileStatement*>(ast->rootBlock->statements[0]);
			EXPECT(whileStatement != nullptr, true);
			AST::IntLiteral* condition = dynamic_cast<AST::IntLiteral*>(whileStatement->condition);
			EXPECT(condition != nullptr, true);
			EXPECT(condition->value, 1);
			EXPECT(whileStatement->body != nullptr, true);
			AST::StatementBlock* body = dynamic_cast<AST::StatementBlock*>(whileStatement->body);
			EXPECT(body != nullptr, true);
			EXPECT((u64)body->statements.size(), (u64)0);

			ast->Destroy();
			delete ast;
		}
		UNIT_TEST_END;

		UNIT_TEST(ParseTestEmptyDoWhile)
		{
			AST::AST* ast = new AST::AST();

			const char* source =
				"do\n{\n} while (1);\n";
			ast->Generate(source);

			EXPECT(ast->diagnosticContainer->diagnostics.empty(), true);
			for (const Diagnostic& diagnostic : ast->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			std::string reconstructedStr = ast->rootBlock->ToString();
			EXPECT(strcmp(reconstructedStr.c_str(), source), 0);

			EXPECT((u64)ast->rootBlock->statements.size(), (u64)1);

			AST::DoWhileStatement* doWhileStatement = dynamic_cast<AST::DoWhileStatement*>(ast->rootBlock->statements[0]);
			EXPECT(doWhileStatement != nullptr, true);
			AST::IntLiteral* condition = dynamic_cast<AST::IntLiteral*>(doWhileStatement->condition);
			EXPECT(condition != nullptr, true);
			EXPECT(condition->value, 1);
			EXPECT(doWhileStatement->body != nullptr, true);
			AST::StatementBlock* body = dynamic_cast<AST::StatementBlock*>(doWhileStatement->body);
			EXPECT(body != nullptr, true);
			EXPECT((u64)body->statements.size(), (u64)0);

			ast->Destroy();
			delete ast;
		}
		UNIT_TEST_END;

		UNIT_TEST(LexAndParseTests)
		{
			AST::AST* ast = new AST::AST();

			ast->Generate("\n"
				"//int abcdefghi = 115615;\n"
				"float basicBit = 55.21235f; int    aaa   =    111111  ;   //\n"
				"/* \n"
				"	 /*blocky \n"
				"		nested comment*/ \n"
				" // func //// int // '); DROP TABLE users \n"
				"*/ \n"
				"\n"
				"func my_function(int index, string name) -> int { \n"
				"	int result = name[index]; \n"
				"	return result; \n"
				"} \n"
				"int result = my_function(1, \"test\"); \n"
				"\n"
				"string str = \"long   string with \\\"lots\\\"  of fun spaces! // /* */ \"; \n"
				"bool b = abcdefghi != 115615 || 7 ^ ~33; \n"
				"int[] list = { 11, 22, 33, 44, 55 }; \n"
				"int chosen_one = list[0*1+2+1];;;;; \n"
				";;;;int a = (2 * 3 + 1) * 4 + 5 - 1 * 50 / 2; \n"
				"bool baby = (1==2) && (2 >= 9) || ((9*6 - 1 < 717)); \n"
				"if ((1 + 1) == 2) { print(\"Calcium!\");;; } \n"
				"elif ((3 * 3) == (8 * 8)) { print(\"Bromine? (%i)\", a); }\n"
				"else { print(\"maganese...\"); }\n"
				"int i = 0;\n"
				"while (1) { print(\"%i\", i); i += 1; if (i > 100) break; print(\"keep going\"); }\n"
				"alpha += 33; basicBit /= 9.0; basicBit *= 2.f; basicBit -= 0.01f; \n\n"
				"basicBit = b ? (basicBit * 3.5f + 7 / aaa) : 0.0f;\n\n");

			EXPECT(ast->diagnosticContainer->diagnostics.empty(), true);
			for (const Diagnostic& diagnostic : ast->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			std::string reconstructedStr = ast->rootBlock->ToString();
			Print("%s\n", reconstructedStr.c_str());

			ast->Destroy();
			delete ast;
		}
		UNIT_TEST_END;

		//
		// VM tests
		//

		UNIT_TEST(VMTestsBasic0)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();

			using ValueType = VM::VariantWrapper::Type;
			using OpCode = VM::OpCode;

			std::vector<VM::Instruction> instStream;

			instStream.push_back({ OpCode::ADD, { ValueType::REGISTER, Variant(0) }, { ValueType::CONSTANT, Variant(1) }, { ValueType::CONSTANT, Variant(2) } }); // r0 = 1 + 2
			instStream.push_back({ OpCode::MUL, { ValueType::REGISTER, Variant(0) }, { ValueType::REGISTER, Variant(0) }, { ValueType::CONSTANT, Variant(2) } }); // r0 = r0 * 2
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::ADD, { ValueType::REGISTER, Variant(1) }, { ValueType::CONSTANT, Variant(12) }, { ValueType::CONSTANT, Variant(20) } }); // r1 = 12 + 20
			instStream.push_back({ OpCode::MOD, { ValueType::REGISTER, Variant(1) }, { ValueType::REGISTER, Variant(1) }, { ValueType::REGISTER, Variant(0) } }); // r1 = r1 % r0
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::DIV, { ValueType::REGISTER, Variant(0) }, { ValueType::REGISTER, Variant(0) }, { ValueType::REGISTER, Variant(1) } }); // r0 = r0 / r1
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::HALT });

			vm->GenerateFromInstStream(instStream);

			vm->Execute();

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);
			for (const Diagnostic& diagnostic : vm->state->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			EXPECT(vm->registers[0].valInt, (1 + 2) * 2);

			vm->Execute();

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);
			EXPECT(vm->registers[1].valInt, (12 + 20) % ((1 + 2) * 2));

			vm->Execute();

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);
			EXPECT(vm->registers[0].valInt, ((1 + 2) * 2) / ((12 + 20) % ((1 + 2) * 2)));

			EXPECT(vm->stack.empty(), true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsBasic1)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();

			using ValueType = VM::VariantWrapper::Type;
			using OpCode = VM::OpCode;

			std::vector<VM::Instruction> instStream;


			/*

			int a = 10;
			int b = 13;
			bool b = (a > b) && (a != 0);
			print("b: %s", b ? "true" : "false");

			  |
			  v

			mov r0 10
			mov r1 13
			gt  r2 a b
			cne r3 a #0
			and r0 r2 r3
			...
			push r0
			call print
			...

			*/

			instStream.push_back({ OpCode::ADD, { ValueType::REGISTER, Variant(0) }, { ValueType::CONSTANT, Variant(1) }, { ValueType::CONSTANT, Variant(2) } }); // r0 = 1 + 2
			instStream.push_back({ OpCode::MUL, { ValueType::REGISTER, Variant(0) }, { ValueType::REGISTER, Variant(0) }, { ValueType::CONSTANT, Variant(2) } }); // r0 = r0 * 2
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::ADD, { ValueType::REGISTER, Variant(1) }, { ValueType::CONSTANT, Variant(12) }, { ValueType::CONSTANT, Variant(20) } }); // r1 = 12 + 20
			instStream.push_back({ OpCode::MOD, { ValueType::REGISTER, Variant(1) }, { ValueType::REGISTER, Variant(1) }, { ValueType::REGISTER, Variant(0) } }); // r1 = r1 % r0
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::DIV, { ValueType::REGISTER, Variant(0) }, { ValueType::REGISTER, Variant(0) }, { ValueType::REGISTER, Variant(1) } }); // r0 = r0 / r1
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::HALT });

			vm->GenerateFromInstStream(instStream);

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)0);
			for (const Diagnostic& diagnostic : vm->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			EXPECT(vm->registers[0].valInt, (1 + 2) * 2);

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)0);
			EXPECT(vm->registers[1].valInt, (12 + 20) % ((1 + 2) * 2));

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)0);
			EXPECT(vm->registers[0].valInt, ((1 + 2) * 2) / ((12 + 20) % ((1 + 2) * 2)));

			EXPECT(vm->stack.empty(), true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsLoop0)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();

			using ValueType = VM::VariantWrapper::Type;
			using OpCode = VM::OpCode;

			std::vector<VM::Instruction> instStream;

			instStream.push_back({ OpCode::MOV, { ValueType::REGISTER, Variant(1) }, { ValueType::CONSTANT, Variant(2) } }); // r1 = 2
			// Loop start
			instStream.push_back({ OpCode::MUL, { ValueType::REGISTER, Variant(1) }, { ValueType::REGISTER, Variant(1) }, { ValueType::CONSTANT, Variant(2) } }); // r1 = r1 * 2
			instStream.push_back({ OpCode::ADD, { ValueType::REGISTER, Variant(0) }, { ValueType::REGISTER, Variant(0) }, { ValueType::CONSTANT, Variant(1) } }); // r0 = r0 + 1
			instStream.push_back({ OpCode::CMP, { ValueType::REGISTER, Variant(0) }, { ValueType::CONSTANT, Variant(10) } }); // ro = r0 - 10
			instStream.push_back({ OpCode::JLT, { ValueType::CONSTANT, Variant(1) } }); // if r0 < 10 jump to line 1
			// Loop end
			instStream.push_back({ OpCode::HALT });

			vm->GenerateFromInstStream(instStream);

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)0);
			for (const Diagnostic& diagnostic : vm->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			EXPECT(vm->registers[0].valInt, 10);
			EXPECT(vm->registers[1].valInt, 2048);

			EXPECT(vm->stack.empty(), true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsFunc0)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();

			using ValueType = VM::VariantWrapper::Type;
			using OpCode = VM::OpCode;

			std::vector<VM::Instruction> instStream;

			/*

			func func0(int arg0, int arg1) -> int {
				arg0 = arg0 * arg1;
				arg0 = func1(arg0);
				return arg0;
			}

			func func1(int arg0) -> int {
				arg0 = arg0 * 2;
				return arg0;
			}

			r0 = func0(3, 5);

			*/

			instStream.push_back({ OpCode::PUSH, { ValueType::CONSTANT, Variant(4) } }); // return to line 4 after func
			instStream.push_back({ OpCode::PUSH, { ValueType::CONSTANT, Variant(5) } }); // arg1
			instStream.push_back({ OpCode::PUSH, { ValueType::CONSTANT, Variant(3) } }); // arg0
			instStream.push_back({ OpCode::CALL, { ValueType::CONSTANT, Variant(6) } }); // call func 0 on line 6
			// resume point
			instStream.push_back({ OpCode::POP, { ValueType::REGISTER, Variant(0) } }); // r0 = return val
			instStream.push_back({ OpCode::HALT });
			// func 0
			instStream.push_back({ OpCode::POP, { ValueType::REGISTER, Variant(0) } }); // r0 = arg0
			instStream.push_back({ OpCode::POP, { ValueType::REGISTER, Variant(1) } }); // r1 = arg1
			instStream.push_back({ OpCode::MUL, { ValueType::REGISTER, Variant(0) }, { ValueType::REGISTER, Variant(0) }, { ValueType::REGISTER, Variant(1) } }); // r0 = r0 * r1
			instStream.push_back({ OpCode::PUSH, { ValueType::CONSTANT, Variant(12) } }); // return to line 12 after func
			instStream.push_back({ OpCode::PUSH, { ValueType::REGISTER, Variant(0) } }); // arg0
			instStream.push_back({ OpCode::CALL, { ValueType::CONSTANT, Variant(14) } }); // call func 1 on line 14
			// resume point
			instStream.push_back({ OpCode::POP, { ValueType::REGISTER, Variant(0) } }); // r0 = return val
			instStream.push_back({ OpCode::RETURN, { ValueType::REGISTER, Variant(0) } }); // return r0
			// func 1
			instStream.push_back({ OpCode::POP, { ValueType::REGISTER, Variant(0) } }); // r0 = arg0
			instStream.push_back({ OpCode::MUL, { ValueType::REGISTER, Variant(0) }, { ValueType::REGISTER, Variant(0) }, { ValueType::CONSTANT, Variant(2) } }); // r0 = r0 * 2
			instStream.push_back({ OpCode::RETURN, { ValueType::REGISTER, Variant(0) } }); // return r0

			vm->GenerateFromInstStream(instStream);

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)0);
			for (const Diagnostic& diagnostic : vm->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			EXPECT(vm->registers[0].valInt, (3 * 5) * 2);

			EXPECT(vm->stack.empty(), true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsBytecodeGenFromAST0)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"func foo() -> int { return 2 * 7 + 9 - 6; }"
				"int bar = foo() * foo() + foo(); \n");

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)0);
			for (const Diagnostic& diagnostic : vm->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			i32 foo = 2 * 7 + 9 - 6; // 17
			EXPECT(vm->registers[6].valInt, foo * foo + foo); // 306
			EXPECT(vm->stack.empty(), true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsBytecodeGenFromAST1)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"func foo() -> float { return 2.00 * 7.F + 9.0f - 6.; }"
				"float bar = foo() * foo() + foo(); \n");

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)0);
			for (const Diagnostic& diagnostic : vm->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			real foo = 2.0f * 7.0f + 9.0f - 6.0f; // 17.0f
			EXPECT(vm->registers[6].valFloat, foo * foo + foo); // 306.0f
			EXPECT(vm->stack.empty(), true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsBytecodeGenFromAST2)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"int[] list = [1, 2, 3, 4];"
				"int val1 = list[1]; \n");

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)0);
			for (const Diagnostic& diagnostic : vm->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			EXPECT(vm->registers[2].valInt, 2);
			EXPECT(vm->stack.empty(), true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsBytecodeGenFromAST3)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"int a = 10;"
				"int b = 50 * 99;"
				"int baz = a * 5 - (a + 12) / b; \n"
				"// (int a = 5; a < bar * baz; a += 2) { baz = 2 * baz + a; } \n");

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)0);
			for (const Diagnostic& diagnostic : vm->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			EXPECT(vm->registers[5].valInt, 10 * 5 - (10 + 12) / (50 * 99));
			EXPECT(vm->stack.empty(), true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsBytecodeGenFromAST4)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"{{int a = func0(99,100); \n"
				"\n"
				"func func1(int arg0) -> int { \n"
				"    return func2(arg0 * 2); \n"
				"} \n"
				"func func0(int arg0, int arg1) -> int { \n"
				"	return func1(arg0 * arg1); \n"
				"} \n"
				"\n"
				"func func2(int arg0) -> int { \n"
				"    return arg0 * 4; \n"
				"}\n"
				"\n"
				"float p = 10.0; \n"
				"int r0 = func0(3, 5); \n"
				"r0 += 10 * 7; \n"
				"r0 += func0(1, 1); \n"
				"int val = factorial(5); \n"
				"func factorial(int aa) -> int { \n"
				"    if (aa <= 1) return 1; \n"
				"    return aa * factorial(aa - 1); \n"
				"} \n"
				"}}\n");

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)0);
			for (const Diagnostic& diagnostic : vm->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			EXPECT(vm->stack.empty(), true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsNotAllPathsReturnValue0)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("func foo() -> int { } \n");

			EXPECT((u64)vm->state->diagnosticContainer->diagnostics.size(), (u64)0);

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size() > 0, true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsNotAllPathsReturnValue1)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"func foo() -> int { \n"
				"  if (1) return 1; \n"
				"} \n");

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size() > 0, true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsMismatchedReturnTypes0)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"func foo() -> int { \n"
				"  if (1 > 0) return 1; \n"
				"  else return 12.0f; \n"
				"} \n");

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size() > 0, true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsMismatchedReturnTypes1)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"func foo() -> string { \n"
				"  if (1 > 0) return 'c'; \n"
				"  else return \"test\"; \n"
				"} \n");

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size() > 0, true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsUnreachableVar0)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"{int a = 10}; \n"
				"int b = a;    \n");

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)1);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsUnreachableVar1)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"func foo(int a, char b) -> float { return 1.0f; } \n"
				"float f = foo(1, 2);    \n"
				"int c = a;    \n"
				"char d = b;    \n");

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)1);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsUinitializedVar0)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"char d; \n");

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)1);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsUinitializedVar1)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"int a = 0; \n"
				"for (int b; ; ) { a += 1; yield a; }\n");

			vm->Execute();

			EXPECT((u64)vm->diagnosticContainer->diagnostics.size(), (u64)1);

			delete vm;
		}
		UNIT_TEST_END;

		//
		// Helper tests
		//

		UNIT_TEST(HelpersDirectories0)
		{
			const std::string str = "";
			std::string resultStrip = StripLeadingDirectories(str);
			std::string resultExtract = ExtractDirectoryString(str);
			EXPECT(resultStrip.c_str(), "");
			EXPECT(resultExtract.c_str(), "");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersDirectories1)
		{
			const std::string str = "C:\\test\\directory\\path_to_file\\sorcerers_stone.txt";
			std::string resultStrip = StripLeadingDirectories(str);
			std::string resultExtract = ExtractDirectoryString(str);
			EXPECT(resultStrip.c_str(), "sorcerers_stone.txt");
			EXPECT(resultExtract.c_str(), "C:\\test\\directory\\path_to_file\\");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersDirectories2)
		{
			const std::string str = "RelativePaths/Are/Fun/Too/";
			std::string resultStrip = StripLeadingDirectories(str);
			std::string resultExtract = ExtractDirectoryString(str);
			EXPECT(resultStrip.c_str(), "");
			EXPECT(resultExtract.c_str(), "RelativePaths/Are/Fun/Too/");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersDirectories3)
		{
			const std::string str = "nebula.exif";
			std::string resultStrip = StripLeadingDirectories(str);
			std::string resultExtract = ExtractDirectoryString(str);
			EXPECT(resultStrip.c_str(), "nebula.exif");
			EXPECT(resultExtract.c_str(), "");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersFileType0)
		{
			const std::string str = "";
			std::string resultStrip = StripFileType(str);
			std::string resultExtract = ExtractFileType(str);
			EXPECT(resultStrip.c_str(), "");
			EXPECT(resultExtract.c_str(), "");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersFileType1)
		{
			const std::string str = "uber_dense.glb";
			std::string resultStrip = StripFileType(str);
			std::string resultExtract = ExtractFileType(str);
			EXPECT(resultStrip.c_str(), "uber_dense");
			EXPECT(resultExtract.c_str(), "glb");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersFileType2)
		{
			const std::string str = "E:\\Users\\path_to_file\\ducks.db";
			std::string resultStrip = StripFileType(str);
			std::string resultExtract = ExtractFileType(str);
			EXPECT(resultStrip.c_str(), "E:\\Users\\path_to_file\\ducks");
			EXPECT(resultExtract.c_str(), "db");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersFileType3)
		{
			const std::string str = "asset.flex";
			std::string resultStrip = StripFileType(str);
			std::string resultExtract = ExtractFileType(str);
			EXPECT(resultStrip.c_str(), "asset");
			EXPECT(resultExtract.c_str(), "flex");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersTrim0)
		{
			const std::string str = "";
			std::string resultTrim = Trim(str);
			std::string resultLeading = TrimLeadingWhitespace(str);
			std::string resultTrailing = TrimTrailingWhitespace(str);
			EXPECT(resultTrim.c_str(), "");
			EXPECT(resultLeading.c_str(), "");
			EXPECT(resultTrailing.c_str(), "");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersTrim1)
		{
			const std::string str = "C:\\Program Files\\Ubuntu\\chmod.sh  ";
			std::string resultTrim = Trim(str);
			std::string resultLeading = TrimLeadingWhitespace(str);
			std::string resultTrailing = TrimTrailingWhitespace(str);
			EXPECT(resultTrim.c_str(), "C:\\Program Files\\Ubuntu\\chmod.sh");
			EXPECT(resultLeading.c_str(), "C:\\Program Files\\Ubuntu\\chmod.sh  ");
			EXPECT(resultTrailing.c_str(), "C:\\Program Files\\Ubuntu\\chmod.sh");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersTrim2)
		{
			const std::string str = " _______  ";
			std::string resultTrim = Trim(str);
			std::string resultLeading = TrimLeadingWhitespace(str);
			std::string resultTrailing = TrimTrailingWhitespace(str);
			EXPECT(resultTrim.c_str(), "_______");
			EXPECT(resultLeading.c_str(), "_______  ");
			EXPECT(resultTrailing.c_str(), " _______");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersTrim3)
		{
			const std::string str = " Edwards, Roger";
			std::string resultTrim = Trim(str);
			std::string resultLeading = TrimLeadingWhitespace(str);
			std::string resultTrailing = TrimTrailingWhitespace(str);
			EXPECT(resultTrim.c_str(), "Edwards, Roger");
			EXPECT(resultLeading.c_str(), "Edwards, Roger");
			EXPECT(resultTrailing.c_str(), " Edwards, Roger");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersSplit0)
		{
			const std::string str = "";
			std::vector<std::string> result = Split(str, ',');
			EXPECT((u32)result.size(), 0u);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersSplit1)
		{
			const std::string str = "Item1, Item2,Item3,Item4";
			std::vector<std::string> result = Split(str, ',');
			EXPECT((u32)result.size(), 4u);
			EXPECT(result[0].c_str(), "Item1");
			EXPECT(result[1].c_str(), " Item2");
			EXPECT(result[2].c_str(), "Item3");
			EXPECT(result[3].c_str(), "Item4");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersSplit2)
		{
			const std::string str = "teriyaki-potato-kimchi-kayak-Cambodia";
			std::vector<std::string> result = Split(str, '-');
			EXPECT((u32)result.size(), 5u);
			EXPECT(result[0].c_str(), "teriyaki");
			EXPECT(result[1].c_str(), "potato");
			EXPECT(result[2].c_str(), "kimchi");
			EXPECT(result[3].c_str(), "kayak");
			EXPECT(result[4].c_str(), "Cambodia");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersSplit3)
		{
			const std::string str = "18.901 3007 290.1 19029.0 1001 456.43";
			std::vector<std::string> result = Split(str, ' ');
			EXPECT((u32)result.size(), 6u);
			EXPECT(result[0].c_str(), "18.901");
			EXPECT(result[1].c_str(), "3007");
			EXPECT(result[2].c_str(), "290.1");
			EXPECT(result[3].c_str(), "19029.0");
			EXPECT(result[4].c_str(), "1001");
			EXPECT(result[5].c_str(), "456.43");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersSplit4)
		{
			const std::string str = "line0\n\nline1\nline2";
			std::vector<std::string> result = Split(str, '\n');
			EXPECT((u32)result.size(), 3u);
			EXPECT(result[0].c_str(), "line0");
			EXPECT(result[1].c_str(), "line1");
			EXPECT(result[2].c_str(), "line2");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersSplitNoStrip0)
		{
			const std::string str = "";
			std::vector<std::string> result = SplitNoStrip(str, '\n');
			EXPECT((u32)result.size(), 1u);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersSplitNoStrip1)
		{
			const std::string str = "\n\n\n";
			std::vector<std::string> result = SplitNoStrip(str, '\n');
			EXPECT((u32)result.size(), 4u);
			EXPECT(result[0].c_str(), "");
			EXPECT(result[1].c_str(), "");
			EXPECT(result[2].c_str(), "");
			EXPECT(result[3].c_str(), "");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersSplitNoStrip2)
		{
			const std::string str = "alpha\n\nbeta\ncharlie";
			std::vector<std::string> result = SplitNoStrip(str, '\n');
			EXPECT((u32)result.size(), 4u);
			EXPECT(result[0].c_str(), "alpha");
			EXPECT(result[1].c_str(), "");
			EXPECT(result[2].c_str(), "beta");
			EXPECT(result[3].c_str(), "charlie");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseInt0)
		{
			const std::string str = "9999";
			i32 result = ParseInt(str);
			EXPECT(result, 9999);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseInt1)
		{
			const std::string str = "-9999";
			i32 result = ParseInt(str);
			EXPECT(result, -9999);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseInt2)
		{
			const std::string str = "0";
			i32 result = ParseInt(str);
			EXPECT(result, 0);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseInt3)
		{
			const std::string str = "2147483647";
			i32 result = ParseInt(str);
			EXPECT(result, 2147483647);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseUInt0)
		{
			const std::string str = "9999";
			u32 result = ParseUInt(str);
			EXPECT(result, (u32)9999);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseUInt1)
		{
			const std::string str = "0003";
			u32 result = ParseUInt(str);
			EXPECT(result, (u32)3);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseUInt2)
		{
			const std::string str = "0";
			u32 result = ParseUInt(str);
			EXPECT(result, (u32)0);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseUInt3)
		{
			const std::string str = "4294967295";
			u32 result = ParseUInt(str);
			EXPECT(result, (u32)4294967295);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseULong0)
		{
			const std::string str = "9999";
			u64 result = ParseULong(str);
			EXPECT(result, (u64)9999);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseULong1)
		{
			const std::string str = "00003";
			u64 result = ParseULong(str);
			EXPECT(result, (u64)3);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseULong2)
		{
			const std::string str = "0";
			u64 result = ParseULong(str);
			EXPECT(result, (u64)0);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersParseULong3)
		{
			const std::string str = "18446744073709551615";
			u64 result = ParseULong(str);
			EXPECT(result, (u64)18446744073709551615u);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersIntToString0)
		{
			i32 num = 9999;
			std::string result = IntToString(num, 0);
			EXPECT(result.c_str(), "9999");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersIntToString1)
		{
			i32 num = -9999;
			std::string result = IntToString(num, 0);
			EXPECT(result.c_str(), "-9999");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersIntToString2)
		{
			i32 num = 0;
			std::string result = IntToString(num, 0);
			EXPECT(result.c_str(), "0");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersIntToString3)
		{
			i32 num = 5;
			std::string result = IntToString(num, 6);
			EXPECT(result.c_str(), "000005");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersIntToString4)
		{
			i32 num = 5;
			std::string result = IntToString(num, 3, '*');
			EXPECT(result.c_str(), "**5");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersIntToString5)
		{
			i32 num = 2147483647;
			std::string result = IntToString(num);
			EXPECT(result.c_str(), "2147483647");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersIntToString6)
		{
			i32 num = (-2147483647 - 1); // MSVC warns if -2147483648 is typed explicitly
			std::string result = IntToString(num);
			EXPECT(result.c_str(), "-2147483648");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersIntToString7)
		{
			i32 num = -64;
			std::string result = IntToString(num, 4);
			EXPECT(result.c_str(), "-0064");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersUIntToString0)
		{
			u32 num = 4294967295;
			std::string result = UIntToString(num);
			EXPECT(result.c_str(), "4294967295");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersULongToString0)
		{
			u64 num = 18446744073709551615u;
			std::string result = ULongToString(num);
			EXPECT(result.c_str(), "18446744073709551615");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersFloatToString0)
		{
			real num = 3.333333f;
			std::string result = FloatToString(num, 2);
			EXPECT(result.c_str(), "3.33");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersFloatToString1)
		{
			real num = 0.0f;
			std::string result = FloatToString(num, 1);
			EXPECT(result.c_str(), "0.0");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersFloatToString2)
		{
			real num = 0.1f;
			std::string result = FloatToString(num, 2);
			EXPECT(result.c_str(), "0.10");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersFloatToString3)
		{
			real num = -1234567.5999f;
			std::string result = FloatToString(num, 1);
			EXPECT(result.c_str(), "-1234567.6");
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersNextNonAlphaNumeric0)
		{
			const std::string str = "";
			i32 result = NextNonAlphaNumeric(str, 0);
			EXPECT(result, -1);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersNextNonAlphaNumeric1)
		{
			const std::string str = "alpha99numeric but spaces aren't!";
			i32 result = NextNonAlphaNumeric(str, 0);
			EXPECT(result, 14);
			result = NextNonAlphaNumeric(str, result + 1);
			EXPECT(result, 18);
			result = NextNonAlphaNumeric(str, result + 1);
			EXPECT(result, 25);
			result = NextNonAlphaNumeric(str, result + 1);
			EXPECT(result, 30);
			result = NextNonAlphaNumeric(str, result + 1);
			EXPECT(result, 32);
			result = NextNonAlphaNumeric(str, result + 1);
			EXPECT(result, -1);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersNextNonAlphaNumeric2)
		{
			const std::string str = "/dracula/beats.wav";
			i32 result = NextNonAlphaNumeric(str, 0);
			EXPECT(result, 0);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersRelativeToAbsolutePath0)
		{
			Platform::RetrieveCurrentWorkingDirectory();

			std::string relativePath = "C:/Users/user.name/Documents/Project/../../Subfolder/File.txt";
			relativePath = RelativePathToAbsolute(relativePath);

			EXPECT(strcmp(relativePath.c_str(), "C:/Users/user.name/Subfolder/File.txt"), 0);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersRelativeToAbsolutePath1)
		{
			Platform::RetrieveCurrentWorkingDirectory();

			std::string relativePath = "Documents/Project/../../Subfolder/File.txt";
			std::string absolutePath = RelativePathToAbsolute(relativePath);

			std::string expected = FlexEngine::s_CurrentWorkingDirectory + "/Subfolder/File.txt";
			EXPECT(strcmp(absolutePath.c_str(), expected.c_str()), 0);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersRelativeToAbsolutePath2)
		{
			Platform::RetrieveCurrentWorkingDirectory();

			std::string relativePath = "../../Subfolder/File.txt";
			std::string absolutePath = RelativePathToAbsolute(relativePath);

			size_t lastSlash = FlexEngine::s_CurrentWorkingDirectory.rfind('/');
			size_t secondLastSlash = FlexEngine::s_CurrentWorkingDirectory.rfind('/', lastSlash - 1);
			std::string shortenedWorkingDirectory = FlexEngine::s_CurrentWorkingDirectory.substr(0, secondLastSlash);
			std::string expected = shortenedWorkingDirectory + "/Subfolder/File.txt";
			EXPECT(strcmp(absolutePath.c_str(), expected.c_str()), 0);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersRelativeToAbsolutePath3)
		{
			Platform::RetrieveCurrentWorkingDirectory();

			std::string relativePath = "Subfolder/../Subfolder/../File.txt";
			std::string absolutePath = RelativePathToAbsolute(relativePath);

			std::string expected = FlexEngine::s_CurrentWorkingDirectory + "/File.txt";
			EXPECT(strcmp(absolutePath.c_str(), expected.c_str()), 0);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersRelativeToAbsolutePath4)
		{
			Platform::RetrieveCurrentWorkingDirectory();

			std::string relativePath = "/file_with_underscores.flx";
			std::string absolutePath = RelativePathToAbsolute(relativePath);

			std::string expected = FlexEngine::s_CurrentWorkingDirectory + "/file_with_underscores.flx";
			EXPECT(strcmp(absolutePath.c_str(), expected.c_str()), 0);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersRelativeToAbsolutePath5)
		{
			Platform::RetrieveCurrentWorkingDirectory();

			std::string relativePath = "assets/textures/../../assets/meshes/../../cube.glb";
			std::string absolutePath = RelativePathToAbsolute(relativePath);

			std::string expected = FlexEngine::s_CurrentWorkingDirectory + "/cube.glb";
			EXPECT(strcmp(absolutePath.c_str(), expected.c_str()), 0);
		}
		UNIT_TEST_END;

		//
		// GUID tests
		//

		UNIT_TEST(GUIDGeneration0)
		{
			GUID a = GUID::FromString("69CB35807A080CA94986D8D301C70BE7");
			GUID a2;
			a2.Data1 = 5298160412427488231;
			a2.Data2 = 7623245620174130345;
			GUID c = GUID::FromString("3C40C6CCE0DAA3914AAD5DBE31B94281");

			EXPECT(a != InvalidGUID, true);
			EXPECT(a2 != InvalidGUID, true);
			EXPECT(c != InvalidGUID, true);

			EXPECT(a == a2, true);
			EXPECT(a.Data1 == a2.Data1, true);
			EXPECT(a.Data2 == a2.Data2, true);
			EXPECT(a != c, true);
		}
		UNIT_TEST_END;

		UNIT_TEST(GUIDGeneration1)
		{
			std::string inputStr = "6E63A06A6FD9D39C4608AF519EFB8A2E";
			GUID a = GUID::FromString(inputStr);
			std::string generatedStr = a.ToString();
			EXPECT(inputStr.compare(generatedStr), 0); // Round trip
		}
		UNIT_TEST_END;

		UNIT_TEST(GUIDGeneration2)
		{
			std::string inputStr = "6E63A06A6FD9D39C4608AF519EFB8A2E";
			GUID a = GUID::FromString(inputStr);
			EXPECT(a.Data1, (u64)5046476147563137582);
			EXPECT(a.Data2, (u64)7954377745869951900);
		}
		UNIT_TEST_END;

		UNIT_TEST(GUIDGeneration3)
		{
			GUID a;
			a.Data1 = (u64)5046476147563137582u;
			a.Data2 = (u64)7954377745869951900u;
			std::string generatedStr = a.ToString();
			EXPECT(generatedStr.compare("6E63A06A6FD9D39C4608AF519EFB8A2E"), 0);
		}
		UNIT_TEST_END;

		UNIT_TEST(InvalidGUID0)
		{
			GUID a = GUID::FromString("invalid-guid-length");
			EXPECT(a == InvalidGUID, true);
		}
		UNIT_TEST_END;

		UNIT_TEST(InvalidGUID1)
		{
			// Only caught in DEBUG
			GUID a = GUID::FromString("invalid-content-but-right-length");
			EXPECT(a == InvalidGUID, true);
		}
		UNIT_TEST_END;

		UNIT_TEST(GUIDLessThan0)
		{
			GUID a;
			a.Data1 = 1u;
			a.Data2 = 0u;
			GUID b;
			b.Data1 = 2u;
			b.Data2 = 0u;
			EXPECT(a < b, true);
		}
		UNIT_TEST_END;

		UNIT_TEST(GUIDNotLessThan0)
		{
			GUID a;
			a.Data1 = 2u;
			a.Data2 = 0u;
			GUID b;
			b.Data1 = 1u;
			b.Data2 = 0u;
			EXPECT(a > b, true);
		}
		UNIT_TEST_END;

		UNIT_TEST(GUIDLessThan1)
		{
			GUID a;
			a.Data1 = 1u;
			a.Data2 = 0u;
			GUID b;
			b.Data1 = 1u;
			b.Data2 = 1u;
			EXPECT(a < b, true);
		}
		UNIT_TEST_END;

		UNIT_TEST(GUIDNotLessThan1)
		{
			GUID a;
			a.Data1 = 0u;
			a.Data2 = 1u;
			GUID b;
			b.Data1 = 2u;
			b.Data2 = 0u;
			EXPECT(a > b, true);
		}
		UNIT_TEST_END;

		//
		// Hash tests
		//

		UNIT_TEST(HashesDeterministic)
		{
			u64 result1 = Hash("test");
			u64 result2 = Hash("test");
			EXPECT(result1, result2);
		}
		UNIT_TEST_END;

		UNIT_TEST(HashEmpty)
		{
			u64 result0 = Hash("");
			u64 result1 = Hash(" ");
			EXPECT(result0 != 0, true);
			EXPECT(result0 != result1, true);
		}
		UNIT_TEST_END;

		UNIT_TEST(HashCompare0)
		{
			u64 result0 = Hash("benign");
			u64 result1 = Hash("blueberry");
			u64 result2 = Hash("bargaining");
			u64 result3 = Hash("basket weaver");
			EXPECT(result0 != result1, true);
			EXPECT(result0 != result2, true);
			EXPECT(result0 != result3, true);
			EXPECT(result1 != result2, true);
			EXPECT(result1 != result3, true);
			EXPECT(result2 != result3, true);
		}
		UNIT_TEST_END;

		// Triangles

		// https://www.desmos.com/calculator/byciziyo4k
		UNIT_TEST(SignedDistanceToTriangle0)
		{
			/*
			   *
			  / \
			 / p \
			*-----*
			*/
			glm::vec3 a(-1.0f, 0.0f, -1.0f);
			glm::vec3 b(0.0f, 0.0f, 1.0f);
			glm::vec3 c(1.0f, 0.0f, -1.0f);
			glm::vec3 p(VEC3_ZERO);
			glm::vec3 closestPoint;
			real dist = SignedDistanceToTriangle(p, a, b, c, closestPoint);

			EXPECT(NearlyEquals(dist, -0.4472135955f, 1e-7f), true);
			EXPECT(NearlyEquals(closestPoint, p, 1e-7f), true);
		}
		UNIT_TEST_END;

		UNIT_TEST(SignedDistanceToTriangle1)
		{
			/*
			   *
			  / \ p
			 /   \
			*-----*
			*/
			glm::vec3 a(-1.0f, 0.0f, -1.0f);
			glm::vec3 b(0.0f, 0.0f, 1.0f);
			glm::vec3 c(1.0f, 0.0f, -1.0f);
			glm::vec3 p(0.5f, 0.0f, 0.5f);
			glm::vec3 closestPoint;
			real dist = SignedDistanceToTriangle(p, a, b, c, closestPoint);

			EXPECT(NearlyEquals(dist, 0.22360679775f, 1e-7f), true);
			EXPECT(NearlyEquals(closestPoint, glm::vec3(0.3f, 0.0f, 0.4f), 1e-7f), true);
		}
		UNIT_TEST_END;

		UNIT_TEST(SignedDistanceToTriangle2)
		{
			/*
			   p

			   *
			  / \
			 /   \
			*-----*
			*/
			glm::vec3 a(-1.0f, 0.0f, -1.0f);
			glm::vec3 b(0.0f, 0.0f, 1.0f);
			glm::vec3 c(1.0f, 0.0f, -1.0f);
			glm::vec3 p(0.0f, 0.0f, 2.0f);
			glm::vec3 closestPoint;
			real dist = SignedDistanceToTriangle(p, a, b, c, closestPoint);

			EXPECT(NearlyEquals(dist, 1.0f, 1e-7f), true);
			EXPECT(NearlyEquals(closestPoint, b, 1e-7f), true);
		}
		UNIT_TEST_END;

		UNIT_TEST(SignedDistanceToTriangle3)
		{
			/*
			   *
			  / \
			 /   \
			*-----*
			        p
			*/
			glm::vec3 a(-1.0f, 0.0f, -1.0f);
			glm::vec3 b(0.0f, 0.0f, 1.0f);
			glm::vec3 c(1.0f, 0.0f, -1.0f);
			glm::vec3 p(1.1f, 0.0f, -1.1f);
			glm::vec3 closestPoint;
			real dist = SignedDistanceToTriangle(p, a, b, c, closestPoint);

			EXPECT(NearlyEquals(dist, 0.141421356237f, 1e-7f), true);
			EXPECT(NearlyEquals(closestPoint, c, 1e-7f), true);
		}
		UNIT_TEST_END;

		UNIT_TEST(TestPrettifyNumbers0)
		{
			std::string result = PrettifyLargeNumber(15'690, 1);

			EXPECT(result.c_str(), "15.7k");
		}
		UNIT_TEST_END;

		UNIT_TEST(TestPrettifyNumbers1)
		{
			std::string result = PrettifyLargeNumber(965'840'305, 2);

			EXPECT(result.c_str(), "965.84M");
		}
		UNIT_TEST_END;

		UNIT_TEST(TestPrettifyNumbers2)
		{
			std::string result = PrettifyLargeNumber(999'999'999'999, 1);

			EXPECT(result.c_str(), "1000.0T");
		}
		UNIT_TEST_END;

		UNIT_TEST(TestPrettifyNumbers3)
		{
			std::string result = PrettifyLargeNumber(999'999'999'999'999, 1);

			EXPECT(result.c_str(), "1000.0Q");
		}
		UNIT_TEST_END;

		UNIT_TEST(TestPrettifyNumbers4)
		{
			std::string result = PrettifyLargeNumber(100'000'015, 2);

			EXPECT(result.c_str(), "100.0M");
		}
		UNIT_TEST_END;

		UNIT_TEST(TestPrettifyNumbers5)
		{
			std::string result = PrettifyLargeNumber(10'845, 3);

			EXPECT(result.c_str(), "10.845k");
		}
		UNIT_TEST_END;

		UNIT_TEST(TestPrettifyNumbers6)
		{
			std::string result = PrettifyLargeNumber(560, 1);

			EXPECT(result.c_str(), "560");
		}
		UNIT_TEST_END;

	public:
		static i32 Run()
		{
			using TestFunc = bool(*)();

			TestFunc funcs[] = {
				// JSON tests
				EmptyFileIsParsed, MinimalFileIsParsed, OneFieldFileIsValid, MissingQuoteFailsToParse, ObjectParsedCorrectly,
				MissingSquareBracketFailsToParse, MissingCurlyBracketFailsToParse, LineCommentIgnored, MultipleFieldsParsedCorrectly,
				JSONArray0, JSONArray1, JSONArray2, JSONArray3, ArrayParsedCorrectly, ArrayMissingCommaInArrayFailsParse, ComplexFileIsValid,
				// Math tests
				RayPlaneIntersectionOriginValid, RayPlaneIntersectionXYValid, RayPlaneIntersectionXY2Valid, RayPlaneIntersectionXY3Valid, MinComponentValid, MaxComponentValid,
				QuaternionsAreNearlyEqual, QuaternionsAreNotNearlyEqual,
				// Virtual Machine tests
				// TODO: Fix up
				//ParseTestBasic1, ParseTestBasic2, ParseTestEmptyFor, ParseTestEmptyWhile, ParseTestEmptyDoWhile,
				//LexAndParseTests,
				//VMTestsBasic0, VMTestsBasic1, VMTestsLoop0, VMTestsFunc0,
				//VMTestsBytecodeGenFromAST0, VMTestsBytecodeGenFromAST1, VMTestsBytecodeGenFromAST2,
				//VMTestsBytecodeGenFromAST3,
				//VMTestsBytecodeGenFromAST4,
				//VMTestsNotAllPathsReturnValue0, VMTestsNotAllPathsReturnValue1, VMTestsMismatchedReturnTypes0, VMTestsMismatchedReturnTypes1,
				//VMTestsUnreachableVar0, VMTestsUnreachableVar1,
				//VMTestsUinitializedVar0, VMTestsUinitializedVar1,
				// Misc tests
				CountSetBitsValid, PoolTests, PairTests,
				// Helper tests
				HelpersDirectories0, HelpersDirectories1, HelpersDirectories2, HelpersDirectories3,
				HelpersFileType0, HelpersFileType1, HelpersFileType2, HelpersFileType3,
				HelpersTrim0, HelpersTrim1, HelpersTrim2, HelpersTrim3,
				HelpersSplit0, HelpersSplit1, HelpersSplit2, HelpersSplit3, HelpersSplit4,
				HelpersSplitNoStrip0, HelpersSplitNoStrip1, HelpersSplitNoStrip2,
				HelpersParseInt0, HelpersParseInt1, HelpersParseInt2, HelpersParseInt3,
				HelpersParseUInt0, HelpersParseUInt1, HelpersParseUInt2, HelpersParseUInt3,
				HelpersParseULong0, HelpersParseULong1, HelpersParseULong2, HelpersParseULong3,
				HelpersIntToString0, HelpersIntToString1, HelpersIntToString2, HelpersIntToString3, HelpersIntToString4, HelpersIntToString5, HelpersIntToString6, HelpersIntToString7,
				HelpersUIntToString0,
				HelpersFloatToString0, HelpersFloatToString1, HelpersFloatToString2, HelpersFloatToString3,
				HelpersNextNonAlphaNumeric0, HelpersNextNonAlphaNumeric1, HelpersNextNonAlphaNumeric2,
				HelpersRelativeToAbsolutePath0, HelpersRelativeToAbsolutePath1, HelpersRelativeToAbsolutePath2,
				HelpersRelativeToAbsolutePath3, HelpersRelativeToAbsolutePath4, HelpersRelativeToAbsolutePath5,
				// GUID tests
				GUIDGeneration0, GUIDGeneration1, GUIDGeneration2, GUIDGeneration3, InvalidGUID1, InvalidGUID1,
				GUIDLessThan0, GUIDNotLessThan0, GUIDLessThan1, GUIDNotLessThan1,
				// Hash tests
				HashesDeterministic, HashEmpty, HashCompare0,
				SignedDistanceToTriangle0, SignedDistanceToTriangle1, SignedDistanceToTriangle2, SignedDistanceToTriangle3,

				TestPrettifyNumbers0, TestPrettifyNumbers1, TestPrettifyNumbers2, TestPrettifyNumbers3, TestPrettifyNumbers4, TestPrettifyNumbers5, TestPrettifyNumbers6,
			};

			Print("Running %u tests...\n", (u32)ARRAY_LENGTH(funcs));
			i32 failedTestCount = 0;
			for (const TestFunc& func : funcs)
			{
				try
				{
					if (!func())
					{
						++failedTestCount;
					}
				}
				catch (std::exception& e)
				{
					PrintError("%s\n", e.what());
					++failedTestCount;
				}
			}

			if (failedTestCount > 0)
			{
				PrintError("%i test%s failed!\n", failedTestCount, failedTestCount > 1 ? "s" : "");
			}
			else
			{
				Print("%u/%u tests passed\n", (u32)ARRAY_LENGTH(funcs), (u32)ARRAY_LENGTH(funcs));
			}
			Print("\n");

			return failedTestCount;
		}
	};
}