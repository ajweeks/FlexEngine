#pragma once

// Localized text. Every string shown to the player lives in resources/loc/<language>.txt (en.txt is the source
// language) as "Key = Text" lines, & code refers to it by key:
//     m_UI.Button("play", rect, LOC(Worlds_Play));
//     player->ShowNotification(LOCF(Notify_CampfireLit, minutes));
// To add a string, add a line to en.txt & use LOC(Key) in code (there's no list of keys in the source).
//
// Each LOC() call site resolves its key to a LocID once (the first time it runs, in a static local), after which
// it's an array index, so LOC() is free to use every frame. Keys missing from the text files show up as the key
// itself & are logged once. The files are reloaded when they change while the game runs.
namespace flex
{
	// An index into the string table (see Loc::Intern), stable for the lifetime of the program
	enum class LocID : u32
	{
		INVALID = 0xFFFFFFFF
	};

	namespace Loc
	{
		// The current language's strings, indexed by LocID (never null)
		extern const char* const* g_Strings;

		inline const char* Get(LocID id) { return g_Strings[(u32)id]; }
		// printf-style formatting of a localized string
		std::string Format(LocID id, ...);

		// Returns the ID for a key, adding it to the table if it's new. Takes a lock & looks the key up, so use the
		// LOC_ID macro (which does it once per call site) rather than calling this every frame.
		LocID Intern(const char* key);
		// The key an ID was interned with
		const char* GetKey(LocID id);

		// Reloads the language files if they've been edited (cheap when they haven't). Call once a frame.
		void Update();

		// Languages are identified by their file name ("en", ...). Returns false (& keeps the current one) if unknown.
		bool SetLanguage(const std::string& code);
		const std::string& GetLanguage();
		u32 GetLanguageCount();
		const char* GetLanguageCode(u32 index);
		// In the language itself (e.g. "English", "Français"), from its Language_Name key
		const char* GetLanguageName(u32 index);

		// The display name of an item from its prefab name (e.g. "mineral deposit tin" -> "Tin"), or the prefab name
		// if it has none. Looked up by comparing names, so call it on events rather than every frame.
		const char* ItemName(const std::string& prefabName);
	} // namespace Loc
} // namespace flex

// The LocID of a key, resolved the first time this line runs
#define LOC_ID(key) ([]() -> ::flex::LocID { static const ::flex::LocID s_LocID = ::flex::Loc::Intern(#key); return s_LocID; }())
#define LOC(key) ::flex::Loc::Get(LOC_ID(key))
#define LOCF(key, ...) ::flex::Loc::Format(LOC_ID(key), __VA_ARGS__)
