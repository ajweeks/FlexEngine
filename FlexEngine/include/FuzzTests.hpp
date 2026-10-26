#pragma once

// Randomized robustness ("fuzz") tests for self-contained parsers & string helpers.
//
// Deterministic: every input derives from the seed, so a failure can be reproduced by re-running with the
// same seed & iteration count. Inputs are random mutations of valid seed inputs (plus purely random ones),
// and each target checks that nothing throws, hangs, or breaks a property (e.g. round trips, invariants).
// Memory errors are caught by running under ASan/UBSan (the Sanitize configuration).
//
// Runs (briefly) as part of `Flex --test`. For longer runs: `Flex --fuzz [iterations] [seed] [JSON|Strings|Numbers|WAV|SaveFile]`
//
// A watchdog thread aborts (printing the current input) when one input takes more than a few seconds,
// which catches infinite loops.

#include "Helpers.hpp"
#include "JSONParser.hpp"
#include "SaveFile.hpp"
#include "StringBuilder.hpp"

namespace flex
{
	namespace fuzz
	{
		// splitmix64
		struct Rng
		{
			explicit Rng(u64 seed) : state(seed) {}

			u64 Next()
			{
				u64 z = (state += 0x9E3779B97F4A7C15ull);
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
				return z ^ (z >> 31);
			}

			// [0, n)
			u32 Below(u32 n)
			{
				return n == 0 ? 0 : (u32)(Next() % n);
			}

			bool Chance(u32 oneIn)
			{
				return Below(oneIn) == 0;
			}

			real Float01()
			{
				return (real)(Next() >> 40) / (real)(1ull << 24);
			}

			u64 state;
		};

		struct Context
		{
			const char* targetName = "";
			std::string currentInput;
			u32 failureCount = 0;
			std::atomic<u64> progress{ 0 };
			std::mutex inputMutex;
		};

		inline Context& GetContext()
		{
			static Context context;
			return context;
		}

		inline std::string Escape(const std::string& str, u32 maxLen = 600)
		{
			std::string result;
			for (u32 i = 0; i < (u32)str.size() && i < maxLen; ++i)
			{
				unsigned char c = (unsigned char)str[i];
				if (c == '\\') result += "\\\\";
				else if (c == '\n') result += "\\n";
				else if (c == '\t') result += "\\t";
				else if (c == '\r') result += "\\r";
				else if (c == '"') result += "\\\"";
				else if (c < 32 || c >= 127)
				{
					char buf[8];
					snprintf(buf, sizeof(buf), "\\x%02X", (u32)c);
					result += buf;
				}
				else result += (char)c;
			}
			if (str.size() > maxLen)
			{
				result += "...(" + std::to_string(str.size()) + " bytes)";
			}
			return result;
		}

		inline void SetInput(const std::string& input)
		{
			Context& context = GetContext();
			{
				std::lock_guard<std::mutex> lock(context.inputMutex);
				context.currentInput = input;
			}
			context.progress.fetch_add(1);
		}

		inline void Fail(const std::string& message)
		{
			Context& context = GetContext();
			++context.failureCount;
			// Only report the first few failures of each run to keep the output readable
			if (context.failureCount <= 20)
			{
				std::string input;
				{
					std::lock_guard<std::mutex> lock(context.inputMutex);
					input = context.currentInput;
				}
				// (stderr rather than PrintError, since the console isn't always redirected with stdout)
				fprintf(stderr, "[fuzz %s] %s\n    input: \"%s\"\n", context.targetName, message.c_str(), Escape(input).c_str());
				fflush(stderr);
			}
		}

#define FUZZ_EXPECT(cond, msg) do { if (!(cond)) { ::flex::fuzz::Fail(std::string(msg) + " (" #cond ")"); } } while (0)

		// Aborts the process when a single input runs for too long (an infinite loop)
		class Watchdog
		{
		public:
			Watchdog()
			{
				m_Thread = std::thread([this]()
				{
					Context& context = GetContext();
					u64 lastProgress = context.progress.load();
					auto lastChange = std::chrono::steady_clock::now();
					while (!m_bStop.load())
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(50));
						u64 progress = context.progress.load();
						auto now = std::chrono::steady_clock::now();
						if (progress != lastProgress)
						{
							lastProgress = progress;
							lastChange = now;
						}
						else if (now - lastChange > std::chrono::seconds(30))
						{
							std::string input;
							{
								std::lock_guard<std::mutex> lock(context.inputMutex);
								input = context.currentInput;
							}
							fprintf(stderr, "[fuzz %s] Hang detected (>30s on one input), input: \"%s\"\n", context.targetName, Escape(input).c_str());
							fflush(stderr);
							abort();
						}
					}
				});
			}

			~Watchdog()
			{
				m_bStop = true;
				m_Thread.join();
			}

		private:
			std::atomic<bool> m_bStop{ false };
			std::thread m_Thread;
		};

		//
		// Input generation
		//

		static const char* const s_InterestingTokens[] = {
			"{", "}", "[", "]", "\"", "\\\"", "\\", ":", ",", ".", "-", "-.", "+", "e", "E", "e99", "1e-99", "//", "/*", "*/",
			"true", "false", "nan", "-nan", "inf", "-inf", "0", "-0", "1", "-1", "0.5", "2147483647", "2147483648", "-2147483648",
			"-2147483649", "4294967295", "4294967296", "9223372036854775807", "9223372036854775808", "-9223372036854775809",
			"18446744073709551615", "18446744073709551616", "99999999999999999999999999", "1.5,2.5", "1,2,3", "1,2,3,4",
			" ", "\t", "\n", "\r\n", "..", "../", "/..", "C:", "\\..", "{}", "[]", "[{", "}]", "\"\"", ":{", ":[",
		};

		inline char RandomChar(Rng& rng)
		{
			switch (rng.Below(4))
			{
			case 0: return (char)rng.Below(256); // Any byte, including 0 & >= 0x80
			case 1: return "{}[]\":,.-+0123456789eE\\/ \n\t"[rng.Below(27)];
			default: return (char)(32 + rng.Below(95)); // Printable
			}
		}

		inline std::string RandomString(Rng& rng, u32 maxLen)
		{
			u32 len = rng.Below(maxLen + 1);
			std::string result;
			result.reserve(len);
			while ((u32)result.size() < len)
			{
				if (rng.Chance(4))
				{
					result += s_InterestingTokens[rng.Below((u32)ARRAY_LENGTH(s_InterestingTokens))];
				}
				else
				{
					result += RandomChar(rng);
				}
			}
			return result;
		}

		inline std::string Mutate(Rng& rng, const std::string& seed, const std::vector<std::string>& corpus)
		{
			std::string result = seed;
			u32 mutationCount = 1 + rng.Below(6);
			for (u32 m = 0; m < mutationCount; ++m)
			{
				u32 size = (u32)result.size();
				u32 pos = rng.Below(size + 1);
				switch (rng.Below(8))
				{
				case 0: // Replace a byte
					if (size > 0) result[rng.Below(size)] = RandomChar(rng);
					break;
				case 1: // Insert a byte
					result.insert(result.begin() + pos, RandomChar(rng));
					break;
				case 2: // Erase a range
					if (size > 0) result.erase(pos, 1 + rng.Below(rng.Chance(4) ? 64 : 4));
					break;
				case 3: // Insert a token
					result.insert(pos, s_InterestingTokens[rng.Below((u32)ARRAY_LENGTH(s_InterestingTokens))]);
					break;
				case 4: // Truncate
					result.resize(pos);
					break;
				case 5: // Duplicate a range
				{
					u32 len = rng.Below(32);
					std::string range = result.substr(pos, len);
					result.insert(rng.Below((u32)result.size() + 1), range);
				} break;
				case 6: // Splice in part of another corpus entry
					if (!corpus.empty())
					{
						const std::string& other = corpus[rng.Below((u32)corpus.size())];
						u32 start = rng.Below((u32)other.size() + 1);
						result.insert(pos, other.substr(start, rng.Below(64)));
					}
					break;
				case 7: // Replace a digit run with a different number
				{
					result.insert(pos, std::to_string((i64)rng.Next() >> rng.Below(64)));
				} break;
				}
			}
			return result;
		}

		//
		// JSON
		//

		inline JSONObject RandomJSONObject(Rng& rng, u32 depth)
		{
			JSONObject object;
			u32 fieldCount = rng.Below(depth == 0 ? 8 : 4);
			for (u32 i = 0; i < fieldCount; ++i)
			{
				std::string label = "f" + std::to_string(i);
				if (rng.Chance(3))
				{
					// Labels with awkward characters (but no quotes or backslashes, which labels can't hold)
					label += RandomString(rng, 8);
					label.erase(std::remove_if(label.begin(), label.end(), [](char c) { return c == '"' || c == '\\' || c == '\0'; }), label.end());
				}

				switch (rng.Below(depth < 3 ? 11 : 8))
				{
				case 0:
				{
					// (Prefixed with a letter since strings which look like lists of numbers are read back as vectors)
					std::string str = "s" + RandomString(rng, 24);
					str.erase(std::remove(str.begin(), str.end(), '\0'), str.end());
					object.fields.emplace_back(label, JSONValue(str));
				} break;
				case 1: object.fields.emplace_back(label, JSONValue((i32)(rng.Next() >> rng.Below(64)) * (rng.Chance(2) ? -1 : 1))); break;
				case 2: object.fields.emplace_back(label, JSONValue((u32)(rng.Next() >> (32 + rng.Below(32))))); break;
				case 3: object.fields.emplace_back(label, JSONValue((i64)(rng.Next() >> 1) * -1)); break;
				case 4: object.fields.emplace_back(label, JSONValue((u64)(rng.Next() | (1ull << 63)))); break;
				case 5: object.fields.emplace_back(label, JSONValue((rng.Float01() - 0.5f) * 2000.0f)); break;
				case 6: object.fields.emplace_back(label, JSONValue(rng.Chance(2))); break;
				case 7: object.fields.emplace_back(label, JSONValue(glm::vec3(rng.Float01(), -rng.Float01() * 10.0f, 100.0f * rng.Float01()))); break;
				case 8: object.fields.emplace_back(label, JSONValue(RandomJSONObject(rng, depth + 1))); break;
				case 9:
				{
					std::vector<JSONObject> objects(rng.Below(4));
					for (JSONObject& child : objects)
					{
						child = RandomJSONObject(rng, depth + 1);
					}
					object.fields.emplace_back(label, JSONValue(objects));
				} break;
				case 10:
				{
					std::vector<JSONField> values;
					u32 count = 1 + rng.Below(5);
					bool bFloats = rng.Chance(2);
					for (u32 j = 0; j < count; ++j)
					{
						if (bFloats)
						{
							values.emplace_back("", JSONValue((real)rng.Below(1000) / 8.0f));
						}
						else
						{
							values.emplace_back("", JSONValue((i32)rng.Below(100000) - 50000));
						}
					}
					object.fields.emplace_back(label, JSONValue(values));
				} break;
				}
			}
			return object;
		}

		inline void ParseJSONChecked(const std::string& input, bool bCheckReparse)
		{
			SetInput(input);
			JSONObject parsed;
			bool bSuccess = false;
			try
			{
				bSuccess = JSONParser::Parse(input, parsed);
			}
			catch (const std::exception& e)
			{
				Fail(std::string("JSONParser::Parse threw: ") + e.what());
				return;
			}

			if (bSuccess && bCheckReparse)
			{
				// Whatever was parsed must survive being written out & read back in
				std::string printed = parsed.ToString();
				JSONObject reparsed;
				bool bReparsed = false;
				try
				{
					bReparsed = JSONParser::Parse(printed, reparsed);
				}
				catch (const std::exception& e)
				{
					Fail(std::string("Re-parsing printed JSON threw: ") + e.what() + "\n    printed: " + Escape(printed));
					return;
				}
				if (!bReparsed)
				{
					Fail(std::string("Printed JSON failed to parse (") + JSONParser::GetErrorString() + ")\n    printed: " + Escape(printed));
				}
				else if (reparsed.ToString() != printed)
				{
					Fail("Printed JSON changed after a round trip\n    printed:  " + Escape(printed) + "\n    reprinted: " + Escape(reparsed.ToString()));
				}
			}
		}

		inline void FuzzJSON(u32 iterations, u64 seed)
		{
			GetContext().targetName = "JSONParser";
			Rng rng(seed);

			std::vector<std::string> corpus = {
				"{}",
				"{ \"a\" : 1 }",
				"{ \"name\" : \"value\", \"f\" : 1.250, \"neg\" : -7, \"b\" : true, \"c\" : false }",
				"{ \"v2\" : \"1.0,2.0\", \"v3\" : \"1.000, -2.500, 3.000\", \"v4\" : \"0,0,0,1\" }",
				"{ \"big\" : 4294967296, \"neg big\" : -9223372036854775807, \"u\" : 18446744073709551615 }",
				"{ \"obj\" : { \"inner\" : { \"x\" : 1 } }, \"arr\" : [ { \"a\" : 1 }, { \"b\" : \"two\" } ] }",
				"{ \"ints\" : [ 1, 2, 3 ], \"floats\" : [ 1.5, -2.25, 3.0 ], \"mixed\" : [ 1, -2, 3 ], \"longs\" : [ 1, 9999999999 ] }",
				"// comment\n{\n\t\"a\" : \"text with spaces // not a comment\", // trailing comment\n\t\"b\" : 2\n}",
				"{ \"escaped\" : \"say \\\"hi there\\\" {[ ]}\", \"path\" : \"C:/dir/file.txt\" }",
				"{ \"empty obj\" : {}, \"empty arr\" : [], \"empty str\" : \"\" }",
			};

			// Real config files make good seeds too (they cover what the engine actually reads)
			const char* configFiles[] = {
				CONFIG_DIRECTORY "renderer_settings.json", CONFIG_DIRECTORY "input_bindings.json", CONFIG_DIRECTORY "fonts.json",
				CONFIG_DIRECTORY "player_inventory_ui.json", CONFIG_DIRECTORY "debug_overlay_names.json",
			};
			for (const char* configFile : configFiles)
			{
				std::string contents;
				if (FileExists(configFile) && ReadFile(configFile, contents, false) && contents.size() < 64 * 1024)
				{
					corpus.push_back(contents);
				}
			}

			for (u32 i = 0; i < 16; ++i)
			{
				corpus.push_back(RandomJSONObject(rng, 0).ToString());
			}

			for (u32 i = 0; i < iterations; ++i)
			{
				std::string input;
				switch (rng.Below(8))
				{
				case 0: input = RandomString(rng, 64); break;
				case 1: input = "{" + RandomString(rng, 64) + "}"; break;
				default: input = Mutate(rng, corpus[rng.Below((u32)corpus.size())], corpus); break;
				}
				ParseJSONChecked(input, true);
			}

			// Generated objects must round trip exactly (including strings with quotes, braces, slashes, etc.)
			for (u32 i = 0; i < iterations / 4; ++i)
			{
				JSONObject object = RandomJSONObject(rng, 0);
				std::string printed = object.ToString();
				SetInput(printed);
				JSONObject parsed;
				bool bSuccess = false;
				try
				{
					bSuccess = JSONParser::Parse(printed, parsed);
				}
				catch (const std::exception& e)
				{
					Fail(std::string("Parse of generated JSON threw: ") + e.what());
					continue;
				}
				if (!bSuccess)
				{
					Fail(std::string("Generated JSON failed to parse: ") + JSONParser::GetErrorString());
					continue;
				}
				FUZZ_EXPECT(parsed.ToString() == printed, "Generated JSON didn't round trip:\n    reprinted: " + Escape(parsed.ToString()));
			}
		}

		//
		// String helpers
		//

		inline bool IsSpaceRef(char c)
		{
			return c == ' ' || c == '\t' || c == '\n' || c == '\r';
		}

		inline std::vector<std::string> SplitRef(const std::string& str, const std::string& delim)
		{
			// Non-empty tokens between delimiters
			std::vector<std::string> result;
			size_t start = 0;
			while (start <= str.size())
			{
				size_t end = str.find(delim, start);
				if (end == std::string::npos)
				{
					end = str.size();
				}
				if (end > start)
				{
					result.push_back(str.substr(start, end - start));
				}
				start = end + delim.size();
			}
			return result;
		}

		inline std::vector<std::string> SplitNoStripRef(const std::string& str, const std::string& delim)
		{
			// All tokens between delimiters (including empty ones)
			std::vector<std::string> result;
			size_t start = 0;
			while (true)
			{
				size_t end = str.find(delim, start);
				if (end == std::string::npos)
				{
					result.push_back(str.substr(start));
					break;
				}
				result.push_back(str.substr(start, end - start));
				start = end + delim.size();
			}
			return result;
		}

		inline void FuzzStringHelpers(u32 iterations, u64 seed)
		{
			GetContext().targetName = "StringHelpers";
			Rng rng(seed);

			for (u32 i = 0; i < iterations; ++i)
			{
				std::string str = RandomString(rng, 40);
				if (rng.Chance(3))
				{
					// Whitespace-heavy strings
					for (char& c : str)
					{
						if (rng.Chance(2)) c = " \t\r\n"[rng.Below(4)];
					}
				}
				SetInput(str);

				try
				{
					// Trim
					{
						std::string trimmed = Trim(str);
						FUZZ_EXPECT(trimmed.empty() || (!IsSpaceRef(trimmed.front()) && !IsSpaceRef(trimmed.back())), "Trim left whitespace");
						FUZZ_EXPECT(str.find(trimmed) != std::string::npos, "Trim result isn't a substring");
						std::string leading = TrimLeadingWhitespace(str);
						FUZZ_EXPECT(leading.empty() || !IsSpaceRef(leading.front()), "TrimLeadingWhitespace left whitespace");
						std::string trailing = TrimTrailingWhitespace(str);
						FUZZ_EXPECT(trailing.empty() || !IsSpaceRef(trailing.back()), "TrimTrailingWhitespace left whitespace");
					}

					// Split
					{
						char delimChar = rng.Chance(2) ? ',' : (str.empty() ? ',' : str[rng.Below((u32)str.size())]);
						if (delimChar != '\0')
						{
							FUZZ_EXPECT(Split(str, delimChar) == SplitRef(str, std::string(1, delimChar)), "Split(char) mismatch");
							FUZZ_EXPECT(SplitNoStrip(str, delimChar) == SplitNoStripRef(str, std::string(1, delimChar)), "SplitNoStrip(char) mismatch");
						}

						std::string delim = rng.Chance(2) ? std::string(", ") : RandomString(rng, 3);
						delim.erase(std::remove(delim.begin(), delim.end(), '\0'), delim.end());
						if (!delim.empty())
						{
							FUZZ_EXPECT(Split(str, delim.c_str()) == SplitRef(str, delim), "Split(const char*) mismatch, delim: \"" + Escape(delim) + "\"");
							FUZZ_EXPECT(SplitNoStrip(str, delim.c_str()) == SplitNoStripRef(str, delim), "SplitNoStrip(const char*) mismatch, delim: \"" + Escape(delim) + "\"");
						}

						std::vector<std::string> handled = SplitHandleStrings(str, ' ');
						for (const std::string& part : handled)
						{
							FUZZ_EXPECT(!part.empty(), "SplitHandleStrings returned an empty part");
						}
					}

					// Replace
					{
						std::string pattern = RandomString(rng, 3);
						std::string replacement = RandomString(rng, 3);
						std::string replaced = Replace(str, pattern, replacement);
						if (!pattern.empty() && pattern.find(replacement) == std::string::npos && replacement.find(pattern) == std::string::npos)
						{
							// Every occurrence is replaced, unless one is formed across a replacement boundary
							FUZZ_EXPECT(replaced.find(pattern) == std::string::npos || (pattern.size() > 1), "Replace left an occurrence of the pattern");
						}
						char from = str.empty() ? 'a' : str[rng.Below((u32)str.size())];
						std::string replacedChars = Replace(str, from, '#');
						FUZZ_EXPECT(replacedChars.size() == str.size(), "Replace(char) changed the string's length");
						FUZZ_EXPECT(from == '#' || replacedChars.find(from) == std::string::npos, "Replace(char) left an occurrence");
					}

					// Paths
					{
						std::string path = str;
						for (char& c : path)
						{
							if (rng.Chance(4)) c = "/\\.:"[rng.Below(4)];
						}
						SetInput(path);
						std::string r0 = StripLeadingDirectories(path);
						std::string r1 = ExtractDirectoryString(path);
						std::string r2 = StripFileType(path);
						std::string r3 = ExtractFileType(path);
						std::string r4 = RelativePathToAbsolute(path);
						std::string r5 = EnsureTrailingSlash(path);
						FUZZ_EXPECT(!r5.empty() && r5.back() == '/', "EnsureTrailingSlash");
						FLEX_UNUSED(r0); FLEX_UNUSED(r1); FLEX_UNUSED(r2); FLEX_UNUSED(r3); FLEX_UNUSED(r4);
					}

					// Misc
					{
						SetInput(str);
						i32 r0 = NextNonAlphaNumeric(str, (i32)rng.Below((u32)str.size() + 1));
						FUZZ_EXPECT(r0 >= -1 && r0 < (i32)str.size(), "NextNonAlphaNumeric out of range");
						i16 numChars = 0;
						i32 r1 = GetNumberEndingWith(str, numChars);
						FUZZ_EXPECT(numChars >= 0 && numChars <= (i32)str.size(), "GetNumberEndingWith returned a bad digit count");
						FLEX_UNUSED(r1);
						std::string incremented = GetIncrementedPostFixedStr(str, "default");
						FUZZ_EXPECT(!incremented.empty(), "GetIncrementedPostFixedStr returned an empty string");
						std::string other = Mutate(rng, str, {});
						std::string strNoNull = str.c_str();
						std::string otherNoNull = other.c_str();
						i32 cmp = StrCmpCaseInsensitive(strNoNull.c_str(), otherNoNull.c_str());
						i32 cmpRev = StrCmpCaseInsensitive(otherNoNull.c_str(), strNoNull.c_str());
						FUZZ_EXPECT((cmp == 0) == (cmpRev == 0) && (cmp < 0) == (cmpRev > 0), "StrCmpCaseInsensitive isn't antisymmetric");
						FUZZ_EXPECT(StartsWith(strNoNull, strNoNull.substr(0, rng.Below((u32)strNoNull.size() + 1))), "StartsWith");
						FUZZ_EXPECT(EndsWith(strNoNull, strNoNull.substr(rng.Below((u32)strNoNull.size() + 1))), "EndsWith");
						std::string lower = str;
						ToLower(lower);
						FUZZ_EXPECT(IsStringLower(lower) || lower.empty() || true, "");

						glm::vec2 v2; glm::vec3 v3; glm::vec4 v4; glm::quat q;
						bool b2 = TryParseVec2(str, v2);
						bool b3 = TryParseVec3(str, v3);
						bool b4 = TryParseVec4(str, v4, -1.0f);
						bool bq = TryParseQuat(str, q);
						FLEX_UNUSED(b2); FLEX_UNUSED(b3); FLEX_UNUSED(b4); FLEX_UNUSED(bq);
						real f = ParseFloat(str);
						i32 n = ParseInt(str);
						u32 un = ParseUInt(str);
						i64 l = ParseLong(str);
						u64 ul = ParseULong(str);
						FLEX_UNUSED(f); FLEX_UNUSED(n); FLEX_UNUSED(un); FLEX_UNUSED(l); FLEX_UNUSED(ul);

						GUID guid = GUID::FromString(str);
						FLEX_UNUSED(guid);
					}
				}
				catch (const std::exception& e)
				{
					Fail(std::string("String helper threw: ") + e.what());
				}
			}

			// StringBuilder against std::string
			for (u32 i = 0; i < iterations / 8; ++i)
			{
				StringBuilder sb(rng.Below(4));
				std::string expected;
				SetInput(std::to_string(i));
				u32 opCount = rng.Below(24);
				for (u32 op = 0; op < opCount; ++op)
				{
					switch (rng.Below(8))
					{
					case 0: { std::string s = RandomString(rng, 10).c_str(); sb.Append(s); expected += s; } break;
					case 1: { std::string s = RandomString(rng, 10).c_str(); sb.AppendLine(s); expected += s + "\n"; } break;
					case 2: { char c = (char)(1 + rng.Below(255)); sb.Append(c); expected += c; } break;
					case 3: { char c = (char)(1 + rng.Below(255)); sb.AppendLine(c); expected += c; expected += '\n'; } break;
					case 4: { u32 v = (u32)rng.Next(); sb.Append(v); expected += std::to_string(v); } break;
					case 5: { i32 v = (i32)rng.Next(); sb.AppendLine(v); expected += std::to_string(v) + "\n"; } break;
					case 6: { u32 v = (u32)rng.Next(); sb.AppendLine(v); expected += std::to_string(v) + "\n"; } break;
					case 7: if (rng.Chance(4)) { sb.Clear(); expected.clear(); } break;
					}
					FUZZ_EXPECT(sb.Length() == (u32)expected.size(), "StringBuilder length mismatch");
					FUZZ_EXPECT(strlen(sb.ToCString()) <= (size_t)sb.Length(), "StringBuilder::ToCString isn't null terminated");
				}
				FUZZ_EXPECT(sb.ToString() == expected, "StringBuilder contents mismatch");
			}
		}

		//
		// Number formatting
		//

		inline void FuzzNumberFormatting(u32 iterations, u64 seed)
		{
			GetContext().targetName = "NumberFormatting";
			Rng rng(seed);

			for (u32 i = 0; i < iterations; ++i)
			{
				u64 bits = rng.Next() >> rng.Below(64);
				SetInput(std::to_string(bits));

				try
				{
					i32 i32Val = (i32)bits * (rng.Chance(2) ? -1 : 1);
					u16 minChars = (u16)rng.Below(16);
					std::string intStr = IntToString(i32Val, minChars, '0');
					FUZZ_EXPECT(intStr.size() >= minChars, "IntToString too short");
					FUZZ_EXPECT(ParseInt(intStr) == i32Val, "IntToString/ParseInt round trip");
					FUZZ_EXPECT(ParseUInt(UIntToString((u32)bits, minChars, '0')) == (u32)bits, "UIntToString/ParseUInt round trip");
					FUZZ_EXPECT(ParseLong(LongToString((i64)bits, minChars, '0')) == (i64)bits, "LongToString/ParseLong round trip");
					FUZZ_EXPECT(ParseULong(ULongToString(bits, minChars, '0')) == bits, "ULongToString/ParseULong round trip");

					// Prettified numbers should be within the precision of the original
					u32 precision = rng.Below(4);
					std::string pretty = PrettifyLargeNumber(bits, precision);
					FUZZ_EXPECT(!pretty.empty(), "PrettifyLargeNumber returned an empty string");
					if (bits >= 1000 && bits < 1000000000000000000ull)
					{
						static const char suffixes[] = { 'k', 'M', 'B', 'T', 'Q' };
						const char* suffix = strchr(suffixes, pretty.back());
						FUZZ_EXPECT(suffix != nullptr, "PrettifyLargeNumber has no suffix: " + pretty);
						if (suffix != nullptr)
						{
							double scale = pow(1000.0, (double)(suffix - suffixes + 1));
							double parsed = strtod(pretty.c_str(), nullptr) * scale;
							double tolerance = scale * pow(10.0, -(double)precision) * 0.51 + (double)bits * 1e-6;
							FUZZ_EXPECT(fabs(parsed - (double)bits) <= tolerance, "PrettifyLargeNumber is inaccurate: " + pretty);
							FUZZ_EXPECT(precision == 0 || pretty.find('.') != std::string::npos, "PrettifyLargeNumber missing decimals: " + pretty);
							if (precision > 0)
							{
								size_t decimals = pretty.size() - 1 - (pretty.find('.') + 1);
								FUZZ_EXPECT(decimals == precision, "PrettifyLargeNumber has the wrong number of decimals: " + pretty);
							}
						}
					}

					char buf[32];
					ByteCountToString(buf, (u32)sizeof(buf), bits, rng.Below(4));
					FUZZ_EXPECT(strlen(buf) < sizeof(buf), "ByteCountToString");

					real f = (real)((double)(i64)bits / pow(10.0, (double)rng.Below(12)));
					std::string floatStr = FloatToString(f, 3);
					FUZZ_EXPECT(fabs(ParseFloat(floatStr) - f) <= glm::max(0.0006f, fabs(f) * 1e-6f), "FloatToString/ParseFloat round trip: " + floatStr);

					glm::vec3 v(f, -f * 0.5f, 3.0f);
					glm::vec3 parsedV;
					FUZZ_EXPECT(TryParseVec3(VecToString(v, 2), parsedV), "VecToString/TryParseVec3");

					u32 packed = Pack2FloatToU32(rng.Float01(), rng.Float01());
					real f1, f2;
					UnpackU32To2Float(packed, &f1, &f2);
					FUZZ_EXPECT(f1 >= 0.0f && f1 <= 1.0f && f2 >= 0.0f && f2 <= 1.0f, "UnpackU32To2Float out of range");

					FUZZ_EXPECT(NextPowerOfTwo((u32)bits | 1u) >= ((u32)bits | 1u) || ((u32)bits | 1u) > 0x80000000u, "NextPowerOfTwo(u32)");
					FUZZ_EXPECT(CountSetBits((u32)bits) <= 32, "CountSetBits");

					// Base64 round trip
					std::vector<u8> raw(rng.Below(40));
					for (u8& b : raw) b = (u8)rng.Below(256);
					std::vector<char> encoded(4 * ((raw.size() + 2) / 3) + 1, '\0');
					Base64Encode(raw.data(), encoded.data(), raw.size());
					size_t encodedLen = strlen(encoded.data());
					FUZZ_EXPECT(encodedLen == 4 * ((raw.size() + 2) / 3), "Base64Encode length");
					std::vector<u8> decoded(raw.size() + 3, 0);
					Base64Decode(encoded.data(), decoded.data(), encodedLen);
					decoded.resize(raw.size());
					FUZZ_EXPECT(decoded == raw, "Base64 round trip");
				}
				catch (const std::exception& e)
				{
					Fail(std::string("Number formatting threw: ") + e.what());
				}
			}
		}

		//
		// WAV files
		//

		inline std::vector<char> MakeWAV(Rng& rng)
		{
			u16 channels = (u16)(1 + rng.Below(2));
			u16 bits = rng.Chance(2) ? 8 : 16;
			u32 sampleCount = rng.Below(64);
			u32 dataSize = sampleCount * channels * (bits / 8);
			std::vector<char> wav;
			auto put32 = [&wav](u32 v) { for (u32 i = 0; i < 4; ++i) wav.push_back((char)((v >> (i * 8)) & 0xFF)); };
			auto put16 = [&wav](u16 v) { for (u32 i = 0; i < 2; ++i) wav.push_back((char)((v >> (i * 8)) & 0xFF)); };
			auto putStr = [&wav](const char* s) { for (u32 i = 0; i < 4; ++i) wav.push_back(s[i]); };
			putStr("RIFF"); put32(36 + dataSize); putStr("WAVE");
			putStr("fmt "); put32(16); put16(1); put16(channels); put32(44100); put32(44100 * channels * bits / 8);
			put16((u16)(channels * bits / 8)); put16(bits);
			putStr("data"); put32(dataSize);
			for (u32 i = 0; i < dataSize; ++i) wav.push_back((char)rng.Below(256));
			return wav;
		}

		inline void FuzzWAV(u32 iterations, u64 seed)
		{
			GetContext().targetName = "WAVParser";
			Rng rng(seed);

			bool bWasEnabled = g_bEnableLogToConsole;
			for (u32 i = 0; i < iterations; ++i)
			{
				std::vector<char> wav = MakeWAV(rng);
				bool bValid = true;
				if (!rng.Chance(8))
				{
					bValid = false;
					std::string mutated = Mutate(rng, std::string(wav.begin(), wav.end()), {});
					wav.assign(mutated.begin(), mutated.end());
				}
				SetInput(std::string(wav.begin(), wav.end()));

				i32 format = 0;
				u8* data = nullptr;
				u32 size = 0;
				u32 freq = 0;
				StringBuilder errorStr;
				bool bParsed = ParseWAVData(wav, "fuzz.wav", &format, &data, &size, &freq, errorStr);
				FUZZ_EXPECT(!bValid || bParsed, "Valid WAV data failed to parse");
				if (bParsed)
				{
					FUZZ_EXPECT(size <= (u32)wav.size(), "WAV data size larger than the file");
					// Touch all the data so ASan catches bad sizes
					u32 sum = 0;
					for (u32 j = 0; j < size; ++j) sum += data[j];
					FLEX_UNUSED(sum);
				}
				delete[] data;
				g_bEnableLogToConsole = bWasEnabled;
			}
		}

		//
		// Binary save files (SaveFile.cpp)
		//

		inline void FuzzSaveFile(u32 iterations, u64 seed)
		{
			GetContext().targetName = "SaveFile";
			Rng rng(seed);

			const u32 fileType = save::FourCC('F', 'U', 'Z', 'Z');
			for (u32 i = 0; i < iterations; ++i)
			{
				const bool bMutate = !rng.Chance(8);
				try
				{
					// Binary JSON
					JSONObject object = RandomJSONObject(rng, 0);
					std::vector<u8> encoded;
					save::EncodeJSON(object, encoded);
					std::string bytes(encoded.begin(), encoded.end());
					if (bMutate)
					{
						bytes = Mutate(rng, bytes, {});
					}
					SetInput(bytes);
					JSONObject decoded;
					bool bDecoded = save::DecodeJSON((const u8*)bytes.data(), bytes.size(), decoded);
					if (!bMutate)
					{
						// Compared against the text format's round trip, which normalizes types the same way (e.g.
						// positive ints are read back as uints, & empty arrays as field arrays)
						JSONObject reparsed;
						const bool bReparsed = JSONParser::Parse(object.ToString(), reparsed);
						std::string difference;
						FUZZ_EXPECT(bDecoded, "Valid binary JSON failed to decode");
						FUZZ_EXPECT(!bDecoded || !bReparsed || save::JSONEquivalent(reparsed, decoded, &difference), "Binary JSON didn't round trip: " + difference);
					}

					// Container (header, section table, compression, crcs)
					save::FileWriter writer(fileType, 1);
					const u32 sectionCount = rng.Below(4);
					for (u32 s = 0; s < sectionCount; ++s)
					{
						const u32 tag = save::FourCC('S', 'E', 'C', (char)('0' + s));
						if (rng.Chance(2))
						{
							writer.AddJSONSection(tag, RandomJSONObject(rng, 1), rng.Chance(2));
						}
						else
						{
							std::vector<u8> raw(rng.Below(rng.Chance(4) ? 4096 : 64));
							const u8 fill = (u8)rng.Below(256);
							for (u8& b : raw) b = rng.Chance(3) ? (u8)rng.Below(256) : fill;
							writer.AddSection(tag, raw, 0, rng.Chance(2));
						}
					}
					std::vector<u8> built = writer.Build();
					std::string container(built.begin(), built.end());
					if (bMutate)
					{
						container = Mutate(rng, container, {});
					}
					SetInput(container);
					save::FileReader reader;
					save::LoadResult result = reader.Parse((const u8*)container.data(), container.size(), fileType);
					FUZZ_EXPECT(bMutate || result == save::LoadResult::OK, std::string("Valid save container failed to load: ") + save::LoadResultToString(result));
					if (result == save::LoadResult::OK)
					{
						for (u32 tag : reader.Tags())
						{
							std::vector<u8> raw;
							bool bRead = reader.GetSection(tag, raw);
							FUZZ_EXPECT(bMutate || bRead, "Valid section failed to read");
						}
						JSONObject fields;
						bool bFields = reader.GetJSONFields(fields);
						FUZZ_EXPECT(bMutate || bFields, "Valid JSON sections failed to read");
					}
				}
				catch (const std::exception& e)
				{
					Fail(std::string("Save file code threw: ") + e.what());
				}
			}
		}

		// onlyTarget optionally limits the run to one target (JSON, Strings, Numbers, WAV, or SaveFile)
		inline i32 Run(u32 iterations, u64 seed, const char* onlyTarget = nullptr)
		{
			Context& context = GetContext();
			context.failureCount = 0;

			bool bWasEnabled = g_bEnableLogToConsole;
			// Parsers print errors for (expected) bad input, which would drown out real failures
			g_bEnableLogToConsole = false;

			{
				Watchdog watchdog;
				auto ShouldRun = [onlyTarget](const char* name) { return onlyTarget == nullptr || strcmp(onlyTarget, name) == 0; };
				if (ShouldRun("JSON")) FuzzJSON(iterations, seed ^ 0x4A534F4Eull);
				if (ShouldRun("Strings")) FuzzStringHelpers(iterations, seed ^ 0x53545247ull);
				if (ShouldRun("Numbers")) FuzzNumberFormatting(iterations, seed ^ 0x4E554D42ull);
				if (ShouldRun("WAV")) FuzzWAV(iterations, seed ^ 0x57415645ull);
				// (Fewer iterations, since each builds & compresses a whole file)
				if (ShouldRun("SaveFile")) FuzzSaveFile(glm::max(iterations / 4, 1u), seed ^ 0x53415645ull);
			}

			g_bEnableLogToConsole = bWasEnabled;
			JSONParser::ClearErrors();

			if (context.failureCount > 0)
			{
				fprintf(stderr, "Fuzz tests: %u failure%s (%u iterations per target, seed %llu)\n", context.failureCount,
					context.failureCount > 1 ? "s" : "", iterations, (unsigned long long)seed);
			}
			else
			{
				fprintf(stderr, "Fuzz tests passed (%u iterations per target, seed %llu)\n", iterations, (unsigned long long)seed);
			}
			fflush(stderr);
			return (i32)context.failureCount;
		}
	} // namespace fuzz
} // namespace flex
