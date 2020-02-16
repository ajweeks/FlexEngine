#include "stdafx.hpp"

#include "JSONParser.hpp"
#include "Helpers.hpp"

namespace flex
{
	class FlexTest
	{
	private:
		using TestFunc = void(*)();

#define JSON_UNIT_TEST(FuncName) static void FuncName() \
	{ \
		const char* FunctionName = #FuncName;

#define JSON_UNIT_TEST_END JSONParser::ClearErrors(); }

#define UNIT_TEST(FuncName) static void FuncName() \
	{ \
		const char* FunctionName = #FuncName;

#define UNIT_TEST_END }


#define EXPECT(val, exp) Expect(FunctionName, val, exp, JSONParser::GetErrorString());

		template<class T>
		static void Expect(const char* funcName, T val, T exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " - Expected " + std::to_string(exp) + ", got " + std::to_string(val) + ", error message:\n\t" + msg;
				throw std::exception(msgStr.c_str());
			}
		}

		template<>
		static void Expect(const char* funcName, glm::vec3 val, glm::vec3 exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " - Expected " + VecToString(exp) + ", got " + VecToString(val) + ", error message:\n\t" + msg;
				throw std::exception(msgStr.c_str());
			}
		}

		template<>
		static void Expect(const char* funcName, const char* val, const char* exp, const char* msg)
		{
			if (strcmp(val, exp) != 0)
			{
				std::string msgStr = std::string(funcName) + " - Expected " + std::string(exp) + ", got " + std::string(val) + ", error message:\n\t" + std::string(msg);
				throw std::exception(msgStr.c_str());
			}
		}

		static void Expect(const char* funcName, JSONValue::Type val, JSONValue::Type exp, const char* msg)
		{
			Expect(funcName, (u32)val, (u32)exp, msg);
		}

		//
		// JSON tests
		//

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
			EXPECT(jsonObj.fields.size(), 1u);
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
			EXPECT(jsonObj.fields.size(), 1u);
			EXPECT(jsonObj.fields[0].label.c_str(), "label");
			EXPECT(jsonObj.fields[0].value.type, JSONValue::Type::OBJECT);
			EXPECT(jsonObj.fields[0].value.objectValue.fields.size(), 1u);
			EXPECT(jsonObj.fields[0].value.objectValue.fields[0].label.c_str(), "sublabel");
			EXPECT(jsonObj.fields[0].value.objectValue.fields[0].value.type, JSONValue::Type::STRING);
			EXPECT(jsonObj.fields[0].value.objectValue.fields[0].value.strValue.c_str(), "childValue1");
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(FieldArrayParsedCorrectly)
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
			EXPECT(jsonObj.fields.size(), 1u);
			EXPECT(jsonObj.fields[0].label.c_str(), "label");
			EXPECT(jsonObj.fields[0].value.type, JSONValue::Type::FIELD_ARRAY);
			EXPECT(jsonObj.fields[0].value.fieldArrayValue.size(), 1u);
			EXPECT(jsonObj.fields[0].value.fieldArrayValue[0].label.c_str(), "array elem 0");
			EXPECT(jsonObj.fields[0].value.fieldArrayValue[0].value.type, JSONValue::Type::FIELD_ENTRY);
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
			EXPECT(jsonObj.fields.size(), 1u);
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
			EXPECT(jsonObj.fields.size(), 2u);
		}
		JSON_UNIT_TEST_END;

		// TODO: Fix error detection and re-enable
		//JSON_UNIT_TEST(MissingCommaFailsParse)
		//{
		//	std::string jsonStr = R"(
		//	{
		//		"label 0" : "value 0"
		//		"label 1" : "value 1"
		//	})";
		//	JSONObject jsonObj;
		//	bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

		//	EXPECT(bSuccess, false);
		//}
		//JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(ArrayParsesCorrectly)
		{
			std::string jsonStr = R"(
			{
				"fields" :
				[
					"label 0" : "value 0",
					"label 1" : "value 1"
				]
			})";
			JSONObject jsonObj;
			bool bSuccess = JSONParser::Parse(jsonStr, jsonObj);

			EXPECT(bSuccess, true);
			EXPECT(jsonObj.fields[0].value.type, JSONValue::Type::FIELD_ARRAY);
			EXPECT(jsonObj.fields[0].value.fieldArrayValue.size(), 2u);
		}
		JSON_UNIT_TEST_END;

		JSON_UNIT_TEST(MissingCommaInArrayFailsParse)
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
							"color" : "1.00, 1.00, 1.00",
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
			EXPECT(jsonObj.fields.size(), 4u);
			EXPECT(jsonObj.fields[2].value.type, JSONValue::Type::BOOL); // Spawn player
			EXPECT(jsonObj.fields[2].value.boolValue, false); // Spawn player is false
			EXPECT(jsonObj.fields[3].value.type, JSONValue::Type::OBJECT_ARRAY);
			EXPECT(jsonObj.fields[3].value.objectArrayValue.size(), 2u); // 2 objects
			EXPECT(jsonObj.fields[3].value.objectArrayValue[0].fields.size(), 6u); // 6 fields in skybox object
			EXPECT(jsonObj.fields[3].value.objectArrayValue[0].fields[4].value.type, JSONValue::Type::FIELD_ARRAY); // skybox materials array
			EXPECT(jsonObj.fields[3].value.objectArrayValue[0].fields[4].value.fieldArrayValue.size(), 1u); // 1 material in materials array
			EXPECT(jsonObj.fields[3].value.objectArrayValue[1].fields[4].label.c_str(), "directional light info"); // Directional light info label
			EXPECT(jsonObj.fields[3].value.objectArrayValue[1].fields[4].value.objectValue.fields[4].value.type, JSONValue::Type::FLOAT); // Directional light brightness type
			EXPECT(jsonObj.fields[3].value.objectArrayValue[1].fields[4].value.objectValue.fields[4].value.floatValue, 3.047f); // Directional light brightness value
		}
		JSON_UNIT_TEST_END;

		//
		// Math tests
		//

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
			glm::vec3 constrainedIntersection = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayOrigin, rayEnd, planeOrigin, planeNorm, startPos, camForward, offset);
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
			glm::vec3 constrainedIntersection = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayOrigin, rayEnd, planeOrigin, planeNorm, startPos, camForward, offset);
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
			glm::vec3 constrainedIntersection = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayOrigin, rayEnd, planeOrigin, planeNorm, startPos, camForward, offset);
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
			glm::vec3 constrainedIntersection = FlexEngine::CalculateRayPlaneIntersectionAlongAxis(axis, rayOrigin, rayEnd, planeOrigin, planeNorm, startPos, camForward, offset);
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

	public:
		static void Run()
		{
			TestFunc funcs[] = {
				// JSON tests
				EmptyFileIsParsed, MinimalFileIsParsed, OneFieldFileIsValid, MissingQuoteFailsToParse, ObjectParsedCorrectly,
				FieldArrayParsedCorrectly, MissingSquareBracketFailsToParse, MissingCurlyBracketFailsToParse, LineCommentIgnored, MultipleFieldsParsedCorrectly,
				ArrayParsesCorrectly, MissingCommaInArrayFailsParse, ComplexFileIsValid,
				// Math tests
				RayPlaneIntersectionOriginValid, RayPlaneIntersectionXYValid, RayPlaneIntersectionXY2Valid, RayPlaneIntersectionXY3Valid, MinComponentValid, MaxComponentValid
			};
			Print("Running %d tests...\n", ARRAY_LENGTH(funcs));
			u32 failedTestCount = 0;
			for (auto func : funcs)
			{
				try
				{
					func();
				}
				catch (std::exception e)
				{
					PrintError("%s\n", e.what());
					failedTestCount += 1;
				}
			}

			if (failedTestCount > 0)
			{
				PrintError("%d test%s failed!\n", failedTestCount, failedTestCount > 1 ? "s" : "");
			}
			else
			{
				Print("%d/%d tests passed\n", ARRAY_LENGTH(funcs), ARRAY_LENGTH(funcs));
			}
			Print("");
		}
	};
}