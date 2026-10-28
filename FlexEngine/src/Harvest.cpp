#include "stdafx.hpp"

#include "Harvest.hpp"

namespace flex
{
	namespace harvest
	{
		static sec s_Clock = 0.0f;
		static std::map<u64, sec> s_Picked; // Key -> clock when picked

		static u64 Key(Kind kind, u32 hash)
		{
			return ((u64)kind << 32) | (u64)hash;
		}

		void Reset()
		{
			s_Clock = 0.0f;
			s_Picked.clear();
		}

		void Tick(sec dt)
		{
			s_Clock += dt;
			// (drop regrown ones now & then to keep the table small)
			if (s_Picked.size() > 64)
			{
				for (auto iter = s_Picked.begin(); iter != s_Picked.end();)
				{
					iter = s_Clock - iter->second >= REGROW_TIME ? s_Picked.erase(iter) : std::next(iter);
				}
			}
		}

		sec TimeUntilReady(Kind kind, u32 sourceHash)
		{
			auto iter = s_Picked.find(Key(kind, sourceHash));
			if (iter == s_Picked.end())
			{
				return 0.0f;
			}
			return glm::max(REGROW_TIME - (s_Clock - iter->second), 0.0f);
		}

		bool IsReady(Kind kind, u32 sourceHash)
		{
			return TimeUntilReady(kind, sourceHash) <= 0.0f;
		}

		void MarkPicked(Kind kind, u32 sourceHash)
		{
			s_Picked[Key(kind, sourceHash)] = s_Clock;
		}

		i32 GetYield(Kind kind, u32 sourceHash)
		{
			const u32 h = (sourceHash * 2654435761u) >> 16;
			return kind == Kind::BERRIES ? 3 + (i32)(h % 3) : 1 + (i32)(h % 3);
		}

		const char* ItemPrefabName(Kind kind)
		{
			return kind == Kind::BERRIES ? "berries" : "coconut";
		}

		std::string Serialize()
		{
			std::string result = std::to_string(s_Clock);
			for (const auto& pair : s_Picked)
			{
				if (s_Clock - pair.second < REGROW_TIME)
				{
					result += ";" + std::to_string(pair.first) + "," + std::to_string(pair.second);
				}
			}
			return result;
		}

		void Deserialize(const std::string& str)
		{
			Reset();
			if (str.empty())
			{
				return;
			}
			const char* c = str.c_str();
			char* end = nullptr;
			s_Clock = (sec)strtod(c, &end);
			c = end;
			while (*c == ';')
			{
				++c;
				const u64 key = strtoull(c, &end, 10);
				if (end == c || *end != ',')
				{
					break;
				}
				c = end + 1;
				const sec at = (sec)strtod(c, &end);
				if (end == c)
				{
					break;
				}
				c = end;
				s_Picked[key] = at;
			}
		}
	} // namespace harvest
} // namespace flex
