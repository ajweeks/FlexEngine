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
		using TestFunc = void(*)();

#define JSON_UNIT_TEST(FuncName) static void FuncName() \
	{ \
		const char* FunctionName = #FuncName;

#define JSON_UNIT_TEST_END JSONParser::ClearErrors(); }

#define UNIT_TEST(FuncName) static void FuncName() \
	{ \
		const char* FunctionName = #FuncName;

#define UNIT_TEST_END }


		template<class T>
		static void Expect(const char* funcName, int lineNumber, T val, T exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + std::to_string(exp) + ", got " + std::to_string(val) + ", error message:\n\t" + msg;
				PrintError("%s\n", msgStr.c_str());
			}
		}

		static void Expect(const char* funcName, int lineNumber, glm::vec3 val, glm::vec3 exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + VecToString(exp) + ", got " + VecToString(val) + ", error message:\n\t" + msg;
				PrintError("%s\n", msgStr.c_str());
			}
		}

		static void Expect(const char* funcName, int lineNumber, std::size_t val, std::size_t exp, const char* msg)
		{
			if (val != exp)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + std::to_string(exp) + ", got " + std::to_string(val) + ", error message:\n\t" + msg;
				PrintError("%s\n", msgStr.c_str());
			}
		}

		static void Expect(const char* funcName, int lineNumber, const char* val, const char* exp, const char* msg)
		{
			if (strcmp(val, exp) != 0)
			{
				std::string msgStr = std::string(funcName) + " L" + std::to_string(lineNumber) + " - Expected " + std::string(exp) + ", got " + std::string(val) + ", error message:\n\t" + std::string(msg);
				PrintError("%s\n", msgStr.c_str());
			}
		}

		static void Expect(const char* funcName, int lineNumber, JSONValue::Type val, JSONValue::Type exp, const char* msg)
		{
			Expect(funcName, lineNumber, (u32)val, (u32)exp, msg);
		}

		//
		// JSON tests
		//

#define EXPECT(val, exp) Expect(FunctionName, __LINE__, val, exp, JSONParser::GetErrorString());

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

#undef EXPECT

		//
		// Math tests
		//

#define EXPECT(val, exp) Expect(FunctionName, __LINE__, val, exp, "");

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

		UNIT_TEST(CountSetBitsValid)
		{
			EXPECT(CountSetBits(0b00000000), 0);
			EXPECT(CountSetBits(0b00000001), 1);
			EXPECT(CountSetBits(0b10000000), 1);
			EXPECT(CountSetBits(0b01010101), 4);
			EXPECT(CountSetBits(0b11111111), 8);
			EXPECT(CountSetBits(0b1111111100000000), 8);
			EXPECT(CountSetBits(0b1111111111111111), 16);
		}
		UNIT_TEST_END;

		//
		// Pool tests
		//

		UNIT_TEST(PoolTests)
		{
			PoolAllocator<real, 4> pool;
			EXPECT(pool.GetPoolSize(), 4);
			EXPECT(pool.GetPoolCount(), 0);

			pool.Alloc();
			pool.Alloc();
			pool.Alloc();
			pool.Alloc();
			EXPECT(pool.GetPoolCount(), 1);
			EXPECT(pool.MemoryUsed(), sizeof(real) * 4);

			pool.Alloc();
			EXPECT(pool.GetPoolCount(), 2);
			EXPECT(pool.MemoryUsed(), sizeof(real) * 8);

			pool.ReleaseAll();
			EXPECT(pool.GetPoolCount(), 0);
			EXPECT(pool.MemoryUsed(), 0);
		}
		UNIT_TEST_END;

		//
		// Pair tests
		//

		UNIT_TEST(PairTests)
		{
			Pair<real, u32> p(1.0f, 23);

			EXPECT(p.first, 1.0f);
			EXPECT(p.second, 23);

			p.first = 2.9f;
			p.second = 992;
			EXPECT(p.first, 2.9f);
			EXPECT(p.second, 992);

			Pair<real, u32> p2 = p;
			EXPECT(p2.first, 2.9f);
			EXPECT(p2.second, 992);

			Pair<real, u32> p3(p);
			EXPECT(p3.first, 2.9f);
			EXPECT(p3.second, 992);

			Pair<real, u32> p4(std::move(p));
			EXPECT(p4.first, 2.9f);
			EXPECT(p4.second, 992);

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

			EXPECT(ast->rootBlock->statements.size(), 1);
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

			EXPECT(ast->rootBlock->statements.size(), 3);

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

			EXPECT(ast->rootBlock->statements.size(), 1);

			AST::ForStatement* forStatement = dynamic_cast<AST::ForStatement*>(ast->rootBlock->statements[0]);
			EXPECT(forStatement != nullptr, true);
			EXPECT(forStatement->setup == nullptr, true);
			EXPECT(forStatement->condition == nullptr, true);
			EXPECT(forStatement->update == nullptr, true);
			EXPECT(forStatement->body != nullptr, true);
			AST::StatementBlock* body = dynamic_cast<AST::StatementBlock*>(forStatement->body);
			EXPECT(body != nullptr, true);
			EXPECT(body->statements.size(), 0);

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

			EXPECT(ast->rootBlock->statements.size(), 1);

			AST::WhileStatement* whileStatement = dynamic_cast<AST::WhileStatement*>(ast->rootBlock->statements[0]);
			EXPECT(whileStatement != nullptr, true);
			AST::IntLiteral* condition = dynamic_cast<AST::IntLiteral*>(whileStatement->condition);
			EXPECT(condition != nullptr, true);
			EXPECT(condition->value, 1);
			EXPECT(whileStatement->body != nullptr, true);
			AST::StatementBlock* body = dynamic_cast<AST::StatementBlock*>(whileStatement->body);
			EXPECT(body != nullptr, true);
			EXPECT(body->statements.size(), 0);

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

			EXPECT(ast->rootBlock->statements.size(), 1);

			AST::DoWhileStatement* doWhileStatement = dynamic_cast<AST::DoWhileStatement*>(ast->rootBlock->statements[0]);
			EXPECT(doWhileStatement != nullptr, true);
			AST::IntLiteral* condition = dynamic_cast<AST::IntLiteral*>(doWhileStatement->condition);
			EXPECT(condition != nullptr, true);
			EXPECT(condition->value, 1);
			EXPECT(doWhileStatement->body != nullptr, true);
			AST::StatementBlock* body = dynamic_cast<AST::StatementBlock*>(doWhileStatement->body);
			EXPECT(body != nullptr, true);
			EXPECT(body->statements.size(), 0);

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

			using ValueType = VM::ValueWrapper::Type;
			using OpCode = VM::OpCode;

			std::vector<VM::Instruction> instStream;

			instStream.push_back({ OpCode::ADD, { ValueType::REGISTER, VM::Value(0) }, { ValueType::CONSTANT, VM::Value(1) }, { ValueType::CONSTANT, VM::Value(2) } }); // r0 = 1 + 2
			instStream.push_back({ OpCode::MUL, { ValueType::REGISTER, VM::Value(0) }, { ValueType::REGISTER, VM::Value(0) }, { ValueType::CONSTANT, VM::Value(2) } }); // r0 = r0 * 2
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::ADD, { ValueType::REGISTER, VM::Value(1) }, { ValueType::CONSTANT, VM::Value(12) }, { ValueType::CONSTANT, VM::Value(20) } }); // r1 = 12 + 20
			instStream.push_back({ OpCode::MOD, { ValueType::REGISTER, VM::Value(1) }, { ValueType::REGISTER, VM::Value(1) }, { ValueType::REGISTER, VM::Value(0) } }); // r1 = r1 % r0
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::DIV, { ValueType::REGISTER, VM::Value(0) }, { ValueType::REGISTER, VM::Value(0) }, { ValueType::REGISTER, VM::Value(1) } }); // r0 = r0 / r1
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::HALT });

			vm->GenerateFromInstStream(instStream);

			vm->Execute();

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);
			for (const Diagnostic& diagnostic : vm->state->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			EXPECT(vm->registers[0].valInt, (1 + 2) * 2);

			vm->Execute();

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);
			EXPECT(vm->registers[1].valInt, (12 + 20) % ((1 + 2) * 2));

			vm->Execute();

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);
			EXPECT(vm->registers[0].valInt, ((1 + 2) * 2) / ((12 + 20) % ((1 + 2) * 2)));

			EXPECT(vm->stack.empty(), true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsBasic1)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();

			using ValueType = VM::ValueWrapper::Type;
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

			instStream.push_back({ OpCode::ADD, { ValueType::REGISTER, VM::Value(0) }, { ValueType::CONSTANT, VM::Value(1) }, { ValueType::CONSTANT, VM::Value(2) } }); // r0 = 1 + 2
			instStream.push_back({ OpCode::MUL, { ValueType::REGISTER, VM::Value(0) }, { ValueType::REGISTER, VM::Value(0) }, { ValueType::CONSTANT, VM::Value(2) } }); // r0 = r0 * 2
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::ADD, { ValueType::REGISTER, VM::Value(1) }, { ValueType::CONSTANT, VM::Value(12) }, { ValueType::CONSTANT, VM::Value(20) } }); // r1 = 12 + 20
			instStream.push_back({ OpCode::MOD, { ValueType::REGISTER, VM::Value(1) }, { ValueType::REGISTER, VM::Value(1) }, { ValueType::REGISTER, VM::Value(0) } }); // r1 = r1 % r0
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::DIV, { ValueType::REGISTER, VM::Value(0) }, { ValueType::REGISTER, VM::Value(0) }, { ValueType::REGISTER, VM::Value(1) } }); // r0 = r0 / r1
			instStream.push_back({ OpCode::YIELD });
			instStream.push_back({ OpCode::HALT });

			vm->GenerateFromInstStream(instStream);

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 0);
			for (const Diagnostic& diagnostic : vm->diagnosticContainer->diagnostics)
			{
				PrintError("L%u: %s\n", diagnostic.span.lineNumber, diagnostic.message.c_str());
			}

			EXPECT(vm->registers[0].valInt, (1 + 2) * 2);

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 0);
			EXPECT(vm->registers[1].valInt, (12 + 20) % ((1 + 2) * 2));

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 0);
			EXPECT(vm->registers[0].valInt, ((1 + 2) * 2) / ((12 + 20) % ((1 + 2) * 2)));

			EXPECT(vm->stack.empty(), true);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsLoop0)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();

			using ValueType = VM::ValueWrapper::Type;
			using OpCode = VM::OpCode;

			std::vector<VM::Instruction> instStream;

			instStream.push_back({ OpCode::MOV, { ValueType::REGISTER, VM::Value(1) }, { ValueType::CONSTANT, VM::Value(2) } }); // r1 = 2
			// Loop start
			instStream.push_back({ OpCode::MUL, { ValueType::REGISTER, VM::Value(1) }, { ValueType::REGISTER, VM::Value(1) }, { ValueType::CONSTANT, VM::Value(2) } }); // r1 = r1 * 2
			instStream.push_back({ OpCode::ADD, { ValueType::REGISTER, VM::Value(0) }, { ValueType::REGISTER, VM::Value(0) }, { ValueType::CONSTANT, VM::Value(1) } }); // r0 = r0 + 1
			instStream.push_back({ OpCode::CMP, { ValueType::REGISTER, VM::Value(0) }, { ValueType::CONSTANT, VM::Value(10) } }); // ro = r0 - 10
			instStream.push_back({ OpCode::JLT, { ValueType::CONSTANT, VM::Value(1) } }); // if r0 < 10 jump to line 1
			// Loop end
			instStream.push_back({ OpCode::HALT });

			vm->GenerateFromInstStream(instStream);

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 0);
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

			using ValueType = VM::ValueWrapper::Type;
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

			instStream.push_back({ OpCode::PUSH, { ValueType::CONSTANT, VM::Value(4) } }); // return to line 4 after func
			instStream.push_back({ OpCode::PUSH, { ValueType::CONSTANT, VM::Value(5) } }); // arg1
			instStream.push_back({ OpCode::PUSH, { ValueType::CONSTANT, VM::Value(3) } }); // arg0
			instStream.push_back({ OpCode::CALL, { ValueType::CONSTANT, VM::Value(6) } }); // call func 0 on line 6
			// resume point
			instStream.push_back({ OpCode::POP, { ValueType::REGISTER, VM::Value(0) } }); // r0 = return val
			instStream.push_back({ OpCode::HALT });
			// func 0
			instStream.push_back({ OpCode::POP, { ValueType::REGISTER, VM::Value(0) } }); // r0 = arg0
			instStream.push_back({ OpCode::POP, { ValueType::REGISTER, VM::Value(1) } }); // r1 = arg1
			instStream.push_back({ OpCode::MUL, { ValueType::REGISTER, VM::Value(0) }, { ValueType::REGISTER, VM::Value(0) }, { ValueType::REGISTER, VM::Value(1) } }); // r0 = r0 * r1
			instStream.push_back({ OpCode::PUSH, { ValueType::CONSTANT, VM::Value(12) } }); // return to line 12 after func
			instStream.push_back({ OpCode::PUSH, { ValueType::REGISTER, VM::Value(0) } }); // arg0
			instStream.push_back({ OpCode::CALL, { ValueType::CONSTANT, VM::Value(14) } }); // call func 1 on line 14
			// resume point
			instStream.push_back({ OpCode::POP, { ValueType::REGISTER, VM::Value(0) } }); // r0 = return val
			instStream.push_back({ OpCode::RETURN, { ValueType::REGISTER, VM::Value(0) } }); // return r0
			// func 1
			instStream.push_back({ OpCode::POP, { ValueType::REGISTER, VM::Value(0) } }); // r0 = arg0
			instStream.push_back({ OpCode::MUL, { ValueType::REGISTER, VM::Value(0) }, { ValueType::REGISTER, VM::Value(0) }, { ValueType::CONSTANT, VM::Value(2) } }); // r0 = r0 * 2
			instStream.push_back({ OpCode::RETURN, { ValueType::REGISTER, VM::Value(0) } }); // return r0

			vm->GenerateFromInstStream(instStream);

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 0);
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

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 0);
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

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 0);
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

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 0);
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

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 0);
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

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 0);
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

			EXPECT(vm->state->diagnosticContainer->diagnostics.size(), 0);

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

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 1);

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

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 1);

			delete vm;
		}
		UNIT_TEST_END;

		UNIT_TEST(VMTestsUinitializedVar0)
		{
			VM::VirtualMachine* vm = new VM::VirtualMachine();
			vm->GenerateFromSource("\n"
				"char d; \n");

			vm->Execute();

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 1);

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

			EXPECT(vm->diagnosticContainer->diagnostics.size(), 1);

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
			EXPECT((u32)result.size(), 0u);
		}
		UNIT_TEST_END;

		UNIT_TEST(HelpersSplitNoStrip1)
		{
			const std::string str = "\n\n\n";
			std::vector<std::string> result = SplitNoStrip(str, '\n');
			EXPECT((u32)result.size(), 3u);
			EXPECT(result[0].c_str(), "");
			EXPECT(result[1].c_str(), "");
			EXPECT(result[2].c_str(), "");
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


	public:
		static void Run()
		{
			TestFunc funcs[] = {
				// JSON tests
				EmptyFileIsParsed, MinimalFileIsParsed, OneFieldFileIsValid, MissingQuoteFailsToParse, ObjectParsedCorrectly,
				FieldArrayParsedCorrectly, MissingSquareBracketFailsToParse, MissingCurlyBracketFailsToParse, LineCommentIgnored, MultipleFieldsParsedCorrectly,
				ArrayParsesCorrectly, MissingCommaInArrayFailsParse, ComplexFileIsValid,
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
				// Helper tests
				HelpersDirectories0, HelpersDirectories1, HelpersDirectories2, HelpersDirectories3,
				HelpersFileType0, HelpersFileType1, HelpersFileType2, HelpersFileType3,
				HelpersTrim0, HelpersTrim1, HelpersTrim2, HelpersTrim3,
				HelpersSplit0, HelpersSplit1, HelpersSplit2, HelpersSplit3, HelpersSplit4,
				HelpersSplitNoStrip0, HelpersSplitNoStrip1, HelpersSplitNoStrip2,
				HelpersNextNonAlphaNumeric0, HelpersNextNonAlphaNumeric1, HelpersNextNonAlphaNumeric2,
				// Misc tests
				CountSetBitsValid, PoolTests, PairTests,
			};
			Print("Running %u tests...\n", (u32)ARRAY_LENGTH(funcs));
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
				PrintError("%u test%s failed!\n", failedTestCount, failedTestCount > 1 ? "s" : "");
			}
			else
			{
				Print("%u/%u tests passed\n", (u32)ARRAY_LENGTH(funcs), (u32)ARRAY_LENGTH(funcs));
			}
			Print("\n");
		}
	};
}