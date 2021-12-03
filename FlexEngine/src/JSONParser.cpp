
#include "stdafx.hpp"

#include "JSONParser.hpp"

#include "Helpers.hpp"
#include "Scene/GameObject.hpp"

namespace flex
{
	std::string JSONParser::s_ErrorStr;

	// TODO: Track line & column index to generate better error messages

	bool JSONParser::ParseFromFile(const std::string& filePath, JSONObject& rootObject)
	{
		std::string fileContents;
		if (!ReadFile(filePath, fileContents, false))
		{
			PrintError("Couldn't find JSON file: %s\n", filePath.c_str());
			return false;
		}

		return Parse(fileContents, rootObject);
	}

	bool JSONParser::Parse(const std::string& fileContents, JSONObject& rootObject)
	{
		JSONParser::ClearErrors();

		std::string dirtyFileContents = fileContents;

		std::string cleanFileContents;
		cleanFileContents.reserve(dirtyFileContents.size());
		bool bInQuote = false;
		i32 i = 0;
		while (i < (i32)dirtyFileContents.size())
		{
			char currentChar = dirtyFileContents[i];
			if (currentChar == '\"')
			{
				bInQuote = !bInQuote;
				cleanFileContents.push_back(currentChar);
			}
			else
			{
				if (bInQuote)
				{
					cleanFileContents.push_back(currentChar);
				}
				else
				{
					// Skip single line comments
					if (currentChar == '/' && dirtyFileContents[i + 1] == '/')
					{
						size_t endLine = dirtyFileContents.find('\n', i + 2);
						if (endLine == std::string::npos)
						{
							endLine = dirtyFileContents.size();
						}

						i += (u32)endLine - i;
						continue;
					}
					// Skip all whitespace characters
					else if (!IsSpace(currentChar))
					{
						cleanFileContents.push_back(currentChar);
					}
				}
			}

			++i;
		}

		rootObject = {};

		if (cleanFileContents == "{}" || cleanFileContents.empty())
		{
			return true; // Empty files are valid
		}

		i32 fileContentOffset = 0;
		bool bParseSucceeded = true;
		bool bParsing = true;
		while (bParsing)
		{
			JSONField field;
			bParsing = ParseField(cleanFileContents, &fileContentOffset, field);
			rootObject.fields.push_back(field);

			bParseSucceeded |= bParsing;

			if (fileContentOffset == (i32)(cleanFileContents.size() - 1))
			{
				// We've reached the end of the file!
				bParsing = false;
			}
		}

		if (!s_ErrorStr.empty())
		{
			bParseSucceeded = false;
		}

		return bParseSucceeded;
	}

	void JSONParser::ClearErrors()
	{
		s_ErrorStr.clear();
	}

	const char* JSONParser::GetErrorString()
	{
		return s_ErrorStr.c_str();
	}

	bool JSONParser::ParseObject(const std::string& fileContents, i32* offset, JSONObject& outObject)
	{
		i32 objectClosingBracket = MatchingBracket('{', fileContents, *offset);
		if (objectClosingBracket == -1)
		{
			s_ErrorStr = "Couldn't find matching bracket for '{'";
			return false;
		}

		if (objectClosingBracket == *offset + 1)
		{
			// Empty block
			*offset = objectClosingBracket + 1;
			return true;
		}

		bool bParsing = true;
		while (bParsing && *offset < objectClosingBracket)
		{
			JSONField field;
			bParsing = ParseField(fileContents, offset, field);
			outObject.fields.push_back(field);
		}

		*offset = objectClosingBracket + 1;

		if (fileContents[*offset] == ',')
		{
			*offset += 1;
		}

		return true;
	}

	bool JSONParser::ReadNumericField(const std::string& fileContents, std::string& outValueStr, i32* offset)
	{
		size_t start = (size_t)*offset;
		size_t nextNonAlphaNumeric = NextNonAlphaNumeric(fileContents, (i32)start + 1);
		size_t length = nextNonAlphaNumeric - start;
		outValueStr = fileContents.substr(start, length);
		if (outValueStr.empty())
		{
			return false;
		}

		if (outValueStr == "nan" || outValueStr == "-nan")
		{
			PrintWarn("Found NaN in json string, replacing with 0\n");
			outValueStr = "0";
		}

		*offset = (i32)nextNonAlphaNumeric;
		return true;
	}

	bool JSONParser::ParseValue(ValueType fieldType, const std::string& fieldName, const std::string& fileContents, size_t quoteEnd, i32* offset, JSONValue& outValue)
	{
		switch (fieldType)
		{
		case ValueType::STRING:
		{
			size_t strQuoteStart = fileContents.find('\"', *offset);

			if (strQuoteStart == std::string::npos)
			{
				s_ErrorStr = "Couldn't find quote after offset " + std::to_string(*offset);
				return false;
			}

			size_t strQuoteEnd = strQuoteStart;
			do
			{
				strQuoteEnd = fileContents.find('\"', strQuoteEnd + 1);
			} while (strQuoteEnd != std::string::npos && fileContents[strQuoteEnd - 1] == '\\');

			if (strQuoteEnd == std::string::npos)
			{
				s_ErrorStr = "Couldn't find end quote after offset " + std::to_string(*offset);
				return false;
			}

			std::string stringValue = fileContents.substr(strQuoteStart + 1, strQuoteEnd - (strQuoteStart + 1));
			stringValue = Replace(stringValue, "\\\"", "\"");
			outValue = JSONValue(stringValue);

			// Is this string actually representing a vector?
			if (Contains(stringValue, ','))
			{
				glm::vec4 vec4Value;
				glm::vec3 vec3Value;
				glm::vec2 vec2Value;
				if (TryParseVec4(stringValue, vec4Value, -1.0f))
				{
					outValue = JSONValue(vec4Value);
				}
				else if (TryParseVec3(stringValue, vec3Value))
				{
					outValue = JSONValue(vec3Value);
				}
				else if (TryParseVec2(stringValue, vec2Value))
				{
					outValue = JSONValue(vec2Value);
				}
			}

			*offset = (i32)strQuoteEnd + 1;
		} break;
		case ValueType::INT:
		{
			std::string valueStr;
			if (!ReadNumericField(fileContents, valueStr, offset))
			{
				return false;
			}

			i32 value = stoi(valueStr);
			outValue = JSONValue(value);
		} break;
		case ValueType::UINT:
		{
			std::string valueStr;
			if (!ReadNumericField(fileContents, valueStr, offset))
			{
				return false;
			}

			u32 value = (u32)stoul(valueStr);
			outValue = JSONValue(value);
		} break;
		case ValueType::LONG:
		{
			std::string valueStr;
			if (!ReadNumericField(fileContents, valueStr, offset))
			{
				return false;
			}

			i64 value = stoll(valueStr);
			outValue = JSONValue(value);
		} break;
		case ValueType::ULONG:
		{
			std::string valueStr;
			if (!ReadNumericField(fileContents, valueStr, offset))
			{
				return false;
			}

			u64 value = stoull(valueStr);
			outValue = JSONValue(value);
		} break;
		case ValueType::FLOAT:
		{
			size_t floatStart = quoteEnd + 2;
			size_t decimalIndex = fileContents.find('.', floatStart);
			size_t floatEnd = NextNonAlphaNumeric(fileContents, (i32)decimalIndex + 1);
			size_t floatCharCount = floatEnd - floatStart;
			std::string floatStr = fileContents.substr(floatStart, floatCharCount);
			real floatValue = 0.0f;
			if (!floatStr.empty())
			{
				if (floatStr == "nan" || floatStr == "-nan")
				{
					PrintWarn("Found NaN in json string, replacing with 0.0f\n");
					floatValue = 0.0f;
				}
				else
				{
					floatValue = stof(floatStr);
				}
			}
			outValue = JSONValue(floatValue);

			*offset = (i32)floatEnd;
		} break;
		case ValueType::BOOL:
		{
			// TODO: Be more strict here? (Require "true" or "false")
			char valueFirstChar = fileContents[quoteEnd + 2];
			bool boolValue = valueFirstChar == 't';
			outValue = JSONValue(boolValue);

			*offset = NextNonAlphaNumeric(fileContents, (i32)quoteEnd + 3);
		} break;
		case ValueType::OBJECT:
		{
			JSONObject object;
			ParseObject(fileContents, offset, object);

			outValue = JSONValue(object);
		} break;
		case ValueType::OBJECT_ARRAY:
		{
			std::vector<JSONObject> objects;

			*offset = (i32)quoteEnd + 2;

			i32 arrayClosingBracket = MatchingBracket('[', fileContents, *offset);
			if (arrayClosingBracket == -1)
			{
				s_ErrorStr = "Couldn't find matching square bracket for " + fieldName;
				return false;
			}

			*offset += 1;

			while (*offset < arrayClosingBracket)
			{
				JSONObject object;
				ParseObject(fileContents, offset, object);

				objects.push_back(object);
			}

			*offset = arrayClosingBracket + 1;

			outValue = JSONValue(objects);
		} break;
		case ValueType::FIELD_ARRAY:
		{
			std::vector<JSONField> fields;

			*offset = (i32)quoteEnd + 2; // Advance past quotes to land on opening bracket

			if (ParseArray(fileContents, quoteEnd, offset, fieldName, fields))
			{
				outValue = JSONValue(fields);
			}
			else
			{
				return false;
			}
		} break;
		case ValueType::UNINITIALIZED:
		default:
		{
			size_t nextNonAlphaNumeric = NextNonAlphaNumeric(fileContents, *offset);
			s_ErrorStr = "Unhandled JSON value type: " + fileContents.substr(quoteEnd + 2, nextNonAlphaNumeric - (quoteEnd + 2));
			*offset = -1;
			return false;
		} break;
		}

		return true;
	}

	bool JSONParser::ParseField(const std::string& fileContents, i32* offset, JSONField& field)
	{
		size_t quoteStart = fileContents.find('\"', *offset);

		if (quoteStart == std::string::npos)
		{
			s_ErrorStr = "Couldn't find opening quote after offset " + std::to_string(*offset);
			return false;
		}

		size_t quoteEnd = fileContents.find('\"', quoteStart + 1);
		if (quoteEnd == std::string::npos)
		{
			s_ErrorStr = "Couldn't find closing quote after offset " + std::to_string(*offset);
			return false;
		}

		field.label = fileContents.substr(quoteStart + 1, quoteEnd - (quoteStart + 1));
		*offset = (i32)quoteEnd + 1;

		if (fileContents[*offset] == ':') // Non field-array field
		{
			*offset += 1; // Advance past colon

			char valueFirstChar = fileContents[*offset];
			ValueType fieldType = JSONValue::TypeFromChar(valueFirstChar, fileContents.substr(*offset + 1));

			JSONValue newValue;
			if (ParseValue(fieldType, field.label, fileContents, quoteEnd, offset, newValue))
			{
				field.value = newValue;
			}
			else
			{
				return false;
			}
		}
		else
		{
			s_ErrorStr = "Invalid json field " + field.label + ", expected ':'";
			return false;
		}

		return true;
	}

	bool JSONParser::ParseArray(const std::string& fileContents, size_t quoteEnd, i32* offset, const std::string& fieldName, std::vector<JSONField>& fields)
	{
		i32 arrayClosingBracket = MatchingBracket('[', fileContents, *offset);
		if (arrayClosingBracket == -1)
		{
			s_ErrorStr = "Couldn't find matching square bracket " + fieldName;
			return false;
		}

		*offset += 1; // Advance past opening bracket

		char valueFirstChar = fileContents[*offset];
		ValueType fieldType = JSONValue::TypeFromChar(valueFirstChar, fileContents.substr(*offset + 1));

		// Check what type of int the data requires
		if (fieldType == ValueType::INT || fieldType == ValueType::UINT ||
			fieldType == ValueType::LONG || fieldType == ValueType::ULONG)
		{
			bool bRequireSigned = false;
			bool bRequireLong = false;

			i32 offsetCopy = *offset;

			while (offsetCopy < arrayClosingBracket)
			{
				ValueType fieldType1 = JSONValue::TypeFromChar(fileContents[offsetCopy], fileContents.substr(offsetCopy + 1));

				JSONValue newValue;
				if (!ParseValue(fieldType1, fieldName, fileContents, quoteEnd, &offsetCopy, newValue))
				{
					return false;
				}

				if (fieldType1 == ValueType::LONG || fieldType1 == ValueType::ULONG)
				{
					bRequireLong = true;
				}

				if (fieldType1 == ValueType::INT || fieldType1 == ValueType::LONG)
				{
					bRequireSigned = true;
				}

				if (fieldType1 == ValueType::ULONG && bRequireSigned)
				{
					s_ErrorStr = "Mismatched integer types found in array";
					return false;
				}

				if (newValue.type != fieldType1)
				{
					s_ErrorStr = "Mismatched types found in array " + fieldName;
					return false;
				}


				if (fileContents[offsetCopy] != ',' &&
					fileContents[offsetCopy] != ']')
				{
					s_ErrorStr = "Expected ',' or ']' after field array entry " + fieldName;
					return false;
				}

				if (fileContents[offsetCopy] == ',')
				{
					offsetCopy += 1;
				}
			}

			if (bRequireLong)
			{
				fieldType = bRequireSigned ? ValueType::LONG : ValueType::ULONG;
			}
			else
			{
				fieldType = bRequireSigned ? ValueType::INT : ValueType::UINT;
			}
		}

		while (*offset < arrayClosingBracket)
		{
			JSONValue newValue;
			if (!ParseValue(fieldType,fieldName, fileContents, quoteEnd, offset, newValue))
			{
				return false;
			}

			if (newValue.type != fieldType)
			{
				s_ErrorStr = "Mismatched types found in array " + fieldName;
				return false;
			}


			if (fileContents[*offset] != ',' &&
				fileContents[*offset] != ']')
			{
				s_ErrorStr = "Expected ',' or ']' after field array entry " + fieldName;
				return false;
			}

			fields.emplace_back("", newValue);

			if (fileContents[*offset] == ',')
			{
				*offset += 1;
			}
		}

		*offset = arrayClosingBracket + 1; // Advance past closing bracket

		return true;
	}

	i32 JSONParser::MatchingBracket(char openingBracket, const std::string& fileContents, i32 offset)
	{
		CHECK_EQ(fileContents[offset], openingBracket);

		char closingBracket;
		if (openingBracket == '[')
		{
			closingBracket = ']';
		}
		else if (openingBracket == '{')
		{
			closingBracket = '}';
		}
		else if (openingBracket == '(')
		{
			closingBracket = ')';
		}
		else
		{
			s_ErrorStr = "Unhandled opening bracket type: " + std::string(openingBracket, 1);
			return -1;
		}

		i32 openSquareBracketCount = 0; // [
		i32 openCurlyBracketCount = 0;  // {
		i32 openParenthesesCount = 0;   // (

		bool bInQuotes = false;
		while (offset < (i32)fileContents.size())
		{
			char currentChar = fileContents[offset];

			if (currentChar == '\"')
			{
				bInQuotes = !bInQuotes;
			}

			if (!bInQuotes)
			{
				if (currentChar == '[')
				{
					++openSquareBracketCount;
				}
				else if (currentChar == ']')
				{
					--openSquareBracketCount;
				}
				else if (currentChar == '{')
				{
					++openCurlyBracketCount;
				}
				else if (currentChar == '}')
				{
					--openCurlyBracketCount;
				}
				else if (currentChar == '(')
				{
					++openParenthesesCount;
				}
				else if (currentChar == ')')
				{
					--openParenthesesCount;
				}

				if (currentChar == closingBracket &&
					openSquareBracketCount == 0 &&
					openCurlyBracketCount == 0 &&
					openParenthesesCount == 0)
				{
					return offset;
				}
			}

			++offset;
		}

		return -1;
	}
} // namespace flex
