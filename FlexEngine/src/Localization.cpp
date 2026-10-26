#include "stdafx.hpp"

#include "Localization.hpp"

#include "Helpers.hpp"

#include "Helpers.hpp"

// Adding a language:
//  1. Write its table: a LocEntry array translating LocIDs, e.g. in a new src/Localization/Loc_fr.cpp:
//         extern const LocEntry g_LocFrench[] = { { LocID::MainMenu_Quit, "Quitter" }, ... };
//         extern const u32 g_LocFrenchCount = ARRAY_LENGTH(g_LocFrench);
//     (any strings it doesn't translate are shown in English)
//  2. Add it to s_Languages below. It then shows up in the settings' Language option.
namespace flex
{
	namespace
	{
		const char* const s_English[] =
		{
#define FLEX_LOC_TEXT(id, english) english,
			FLEX_LOC_STRINGS(FLEX_LOC_TEXT)
#undef FLEX_LOC_TEXT
		};
		static_assert(ARRAY_LENGTH(s_English) == (u32)LocID::_COUNT, "s_English must have every string");

		struct LanguageInfo
		{
			const char* code;
			const char* name; // In the language itself
			const LocEntry* entries; // Null for English (the source language)
			u32 entryCount;
		};

		const LanguageInfo s_Languages[] =
		{
			{ "en", "English", nullptr, 0 },
		};

		// The current language's strings: English, overwritten by the language's translations
		const char* s_Current[(u32)LocID::_COUNT];
		std::string s_CurrentCode;

		struct InitEnglish
		{
			InitEnglish()
			{
				for (u32 i = 0; i < (u32)LocID::_COUNT; ++i)
				{
					s_Current[i] = s_English[i];
				}
				s_CurrentCode = "en";
			}
		};
		InitEnglish s_InitEnglish;

		struct ItemNameEntry
		{
			const char* prefabName;
			LocID id;
		};

		const ItemNameEntry s_ItemNames[] =
		{
			{ "pickaxe", LocID::Item_Pickaxe },
			{ "axe", LocID::Item_Axe },
			{ "shovel", LocID::Item_Shovel },
			{ "wood", LocID::Item_Wood },
			{ "dune buggy", LocID::Item_DuneBuggy },
			{ "row boat", LocID::Item_RowBoat },
			{ "headlamp", LocID::Item_Headlamp },
			{ "solar panel", LocID::Item_SolarPanel },
			{ "battery", LocID::Item_Battery },
			{ "lamp", LocID::Item_Lamp },
			{ "mineral deposit quartz", LocID::Item_Quartz },
			{ "mineral deposit aluminum", LocID::Item_Aluminum },
			{ "mineral deposit tin", LocID::Item_Tin },
			{ "mineral deposit olivine", LocID::Item_Olivine },
			{ "mineral deposit iron", LocID::Item_Iron },
			{ "mineral deposit silicon", LocID::Item_Silicon },
			{ "mineral deposit stone", LocID::Item_Stone },
		};
	} // anonymous namespace

	namespace Loc
	{
		const char* const* g_Strings = s_Current;

		std::string Format(LocID id, ...)
		{
			char buffer[512];
			va_list args;
			va_start(args, id);
			vsnprintf(buffer, ARRAY_LENGTH(buffer), Get(id), args);
			va_end(args);
			return std::string(buffer);
		}

		bool SetLanguage(const std::string& code)
		{
			for (const LanguageInfo& language : s_Languages)
			{
				if (code != language.code)
				{
					continue;
				}
				for (u32 i = 0; i < (u32)LocID::_COUNT; ++i)
				{
					s_Current[i] = s_English[i];
				}
				for (u32 i = 0; i < language.entryCount; ++i)
				{
					const LocEntry& entry = language.entries[i];
					if ((u32)entry.id < (u32)LocID::_COUNT && entry.text != nullptr)
					{
						s_Current[(u32)entry.id] = entry.text;
					}
				}
				s_CurrentCode = language.code;
				return true;
			}
			return false;
		}

		const std::string& GetLanguage()
		{
			return s_CurrentCode;
		}

		u32 GetLanguageCount()
		{
			return (u32)ARRAY_LENGTH(s_Languages);
		}

		const char* GetLanguageCode(u32 index)
		{
			return index < GetLanguageCount() ? s_Languages[index].code : "";
		}

		const char* GetLanguageName(u32 index)
		{
			return index < GetLanguageCount() ? s_Languages[index].name : "";
		}

		const char* ItemName(const std::string& prefabName)
		{
			for (const ItemNameEntry& entry : s_ItemNames)
			{
				if (StrCmpCaseInsensitive(prefabName.c_str(), entry.prefabName) == 0)
				{
					return Get(entry.id);
				}
			}
			return prefabName.c_str();
		}
	} // namespace Loc
} // namespace flex
