#pragma once

#include "LocStrings.hpp"

// Localized text. Strings are identified by a LocID (generated from LocStrings.hpp) & looked up by index in the
// current language's table, so there's no hashing or searching at runtime:
//     m_UI.Button("play", rect, LOC(Worlds_Play));
//     player->ShowNotification(LOCF(Notify_CampfireLit, minutes));
// See LocStrings.hpp for adding strings, & Localization.cpp for adding languages.
namespace flex
{
	enum class LocID : u32
	{
#define FLEX_LOC_ENUM(id, english) id,
		FLEX_LOC_STRINGS(FLEX_LOC_ENUM)
#undef FLEX_LOC_ENUM
		_COUNT
	};

	// A translation, in a language's table (see Localization.cpp)
	struct LocEntry
	{
		LocID id;
		const char* text;
	};

	namespace Loc
	{
		// The current language's strings, indexed by LocID (never null)
		extern const char* const* g_Strings;

		inline const char* Get(LocID id) { return g_Strings[(u32)id]; }
		// printf-style formatting of a localized string
		std::string Format(LocID id, ...);

		// Languages are identified by their code ("en", ...). Returns false (& keeps the current one) if unknown.
		bool SetLanguage(const std::string& code);
		const std::string& GetLanguage();
		u32 GetLanguageCount();
		const char* GetLanguageCode(u32 index);
		// In the language itself (e.g. "English", "Français")
		const char* GetLanguageName(u32 index);

		// The display name of an item from its prefab name (e.g. "mineral deposit tin" -> "Tin"), or the prefab name
		// if it has none. Looked up by comparing names, so call it on events rather than every frame.
		const char* ItemName(const std::string& prefabName);
	} // namespace Loc
} // namespace flex

#define LOC(id) ::flex::Loc::Get(::flex::LocID::id)
#define LOCF(id, ...) ::flex::Loc::Format(::flex::LocID::id, __VA_ARGS__)
