#include "stdafx.hpp"

#include "Localization.hpp"

#include "Helpers.hpp"
#include "Platform/Platform.hpp"

// Adding a language: copy resources/loc/en.txt to <code>.txt (e.g. fr.txt), set its Language_Name & translate its
// lines. It then shows up in the settings' Language option. Any keys it leaves out are shown in English.
namespace flex
{
	namespace
	{
		const char* const LOC_DIRECTORY = RESOURCE_DIRECTORY "loc/";
		const char* const SOURCE_LANGUAGE = "en";
		// More than enough: the table is a fixed array so lookups never race with it growing
		const u32 MAX_STRINGS = 4096;

		struct LanguageInfo
		{
			std::string code;
			std::string name;
		};

		std::mutex s_Mutex;
		bool s_bLoaded = false;

		// Indexed by LocID
		const char* s_Table[MAX_STRINGS];
		std::vector<std::string> s_Keys;
		std::vector<bool> s_bUsed; // Interned by code (keys only in the files aren't reported missing)
		std::vector<bool> s_bReportedMissing;
		std::unordered_map<std::string, u32> s_KeyIndices;
		// Every string the table has pointed at. Never freed, so a pointer from Loc::Get stays valid even if the
		// files are reloaded while it's in use (reloads only happen when a file is edited, so this barely grows).
		std::deque<std::string> s_Storage;

		std::vector<LanguageInfo> s_Languages;
		std::string s_CurrentCode = SOURCE_LANGUAGE;
		DirectoryWatcher* s_Watcher = nullptr;
		// Frames until a changed file is reloaded (-1 when none has changed). Waiting lets editors finish writing it.
		i32 s_ReloadCountdown = -1;

		using KeyValues = std::unordered_map<std::string, std::string>;

		const char* Store(const std::string& text)
		{
			s_Storage.push_back(text);
			return s_Storage.back().c_str();
		}

		std::string Trim(const std::string& str)
		{
			size_t start = str.find_first_not_of(" \t");
			if (start == std::string::npos)
			{
				return "";
			}
			size_t end = str.find_last_not_of(" \t");
			return str.substr(start, end - start + 1);
		}

		std::string Unescape(const std::string& str)
		{
			std::string result;
			result.reserve(str.size());
			for (size_t i = 0; i < str.size(); ++i)
			{
				if (str[i] == '\\' && i + 1 < str.size())
				{
					char next = str[++i];
					switch (next)
					{
					case 'n': result += '\n'; break;
					case 't': result += '\t'; break;
					default: result += next; break; // \\ & \"
					}
				}
				else
				{
					result += str[i];
				}
			}
			return result;
		}

		std::string LanguageFilePath(const std::string& code)
		{
			return std::string(LOC_DIRECTORY) + code + ".txt";
		}

		// Reads "Key = Text" lines
		bool ParseFile(const std::string& filePath, KeyValues& outKeyValues)
		{
			std::string contents;
			if (!ReadFile(filePath, contents, false))
			{
				return false;
			}
			if (contents.compare(0, 3, "\xEF\xBB\xBF") == 0)
			{
				contents.erase(0, 3);
			}

			std::istringstream stream(contents);
			std::string line;
			u32 lineNumber = 0;
			while (std::getline(stream, line))
			{
				++lineNumber;
				if (!line.empty() && line.back() == '\r')
				{
					line.pop_back();
				}
				std::string trimmed = Trim(line);
				if (trimmed.empty() || trimmed[0] == '#')
				{
					continue;
				}
				size_t equals = trimmed.find('=');
				if (equals == std::string::npos)
				{
					PrintWarn("%s:%u: Expected \"Key = Text\"\n", filePath.c_str(), lineNumber);
					continue;
				}
				std::string key = Trim(trimmed.substr(0, equals));
				std::string text = Trim(trimmed.substr(equals + 1));
				if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
				{
					text = text.substr(1, text.size() - 2);
				}
				if (outKeyValues.find(key) != outKeyValues.end())
				{
					PrintWarn("%s:%u: Duplicate key %s\n", filePath.c_str(), lineNumber, key.c_str());
				}
				outKeyValues[key] = Unescape(text);
			}
			return true;
		}

		// Requires s_Mutex
		u32 InternLocked(const std::string& key)
		{
			auto iter = s_KeyIndices.find(key);
			if (iter != s_KeyIndices.end())
			{
				return iter->second;
			}
			if (s_Keys.size() >= MAX_STRINGS)
			{
				PrintError("Too many localized strings (%u), can't add %s\n", MAX_STRINGS, key.c_str());
				return MAX_STRINGS - 1;
			}
			u32 index = (u32)s_Keys.size();
			s_Keys.push_back(key);
			s_bUsed.push_back(false);
			s_bReportedMissing.push_back(false);
			s_KeyIndices.emplace(key, index);
			// Shown until a file provides it
			s_Table[index] = Store(key);
			return index;
		}

		void ReportMissing(u32 index)
		{
			if (s_bLoaded && s_bUsed[index] && !s_bReportedMissing[index])
			{
				s_bReportedMissing[index] = true;
				PrintError("Missing localized string: %s (add it to %sen.txt)\n", s_Keys[index].c_str(), LOC_DIRECTORY);
			}
		}

		// Requires s_Mutex. (Re)reads the language files & updates every string in the table.
		void LoadLocked()
		{
			KeyValues source;
			if (!ParseFile(LanguageFilePath(SOURCE_LANGUAGE), source) || source.empty())
			{
				PrintError("Failed to read localized strings from %s\n", LanguageFilePath(SOURCE_LANGUAGE).c_str());
				if (s_bLoaded)
				{
					// Keeps the current strings rather than replacing them all with keys (e.g. if read mid-save)
					return;
				}
			}

			s_Languages.clear();
			s_Languages.push_back(LanguageInfo{ SOURCE_LANGUAGE, "English" });
			auto nameIter = source.find("Language_Name");
			if (nameIter != source.end())
			{
				s_Languages[0].name = nameIter->second;
			}

			KeyValues current;
			std::vector<std::string> filePaths;
			if (Platform::FindFilesInDirectory(LOC_DIRECTORY, filePaths, "txt"))
			{
				std::sort(filePaths.begin(), filePaths.end());
				for (const std::string& filePath : filePaths)
				{
					std::string code = StripFileType(StripLeadingDirectories(filePath));
					if (code == SOURCE_LANGUAGE)
					{
						continue;
					}
					KeyValues keyValues;
					if (!ParseFile(filePath, keyValues))
					{
						continue;
					}
					auto languageName = keyValues.find("Language_Name");
					s_Languages.push_back(LanguageInfo{ code, languageName != keyValues.end() ? languageName->second : code });
					if (code == s_CurrentCode)
					{
						current = std::move(keyValues);
					}
				}
			}

			bool bCurrentFound = false;
			for (const LanguageInfo& language : s_Languages)
			{
				bCurrentFound = bCurrentFound || language.code == s_CurrentCode;
			}
			if (!bCurrentFound)
			{
				s_CurrentCode = SOURCE_LANGUAGE;
			}

			for (const auto& pair : source)
			{
				InternLocked(pair.first);
			}
			for (const auto& pair : current)
			{
				InternLocked(pair.first);
			}

			s_bLoaded = true;
			for (u32 i = 0; i < (u32)s_Keys.size(); ++i)
			{
				const std::string& key = s_Keys[i];
				auto currentIter = current.find(key);
				auto sourceIter = source.find(key);
				if (sourceIter == source.end())
				{
					ReportMissing(i);
				}
				const std::string& text = currentIter != current.end() ? currentIter->second : (sourceIter != source.end() ? sourceIter->second : key);
				if (text != s_Table[i])
				{
					s_Table[i] = Store(text);
				}
			}
		}

		void EnsureLoadedLocked()
		{
			if (!s_bLoaded)
			{
				LoadLocked();
				s_Watcher = new DirectoryWatcher(LOC_DIRECTORY, false);
			}
		}
	} // anonymous namespace

	namespace Loc
	{
		const char* const* g_Strings = s_Table;

		LocID Intern(const char* key)
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			EnsureLoadedLocked();
			const bool bInFiles = s_KeyIndices.find(key) != s_KeyIndices.end();
			const u32 index = InternLocked(key);
			s_bUsed[index] = true;
			if (!bInFiles)
			{
				ReportMissing(index);
			}
			return (LocID)index;
		}

		const char* GetKey(LocID id)
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			return (u32)id < (u32)s_Keys.size() ? s_Keys[(u32)id].c_str() : "";
		}

		std::string Format(LocID id, ...)
		{
			char buffer[512];
			va_list args;
			va_start(args, id);
			vsnprintf(buffer, ARRAY_LENGTH(buffer), Get(id), args);
			va_end(args);
			return std::string(buffer);
		}

		void Update()
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			if (s_Watcher == nullptr || !s_Watcher->Installed())
			{
				return;
			}
			if (s_Watcher->Update())
			{
				for (const std::string& filePath : s_Watcher->modifiedFilePaths)
				{
					if (ExtractFileType(filePath) == "txt")
					{
						s_ReloadCountdown = 10;
						break;
					}
				}
			}
			if (s_ReloadCountdown >= 0 && --s_ReloadCountdown < 0)
			{
				LoadLocked();
				Print("Reloaded localized strings\n");
			}
		}

		bool SetLanguage(const std::string& code)
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			EnsureLoadedLocked();
			for (const LanguageInfo& language : s_Languages)
			{
				if (language.code == code)
				{
					if (code != s_CurrentCode)
					{
						s_CurrentCode = code;
						LoadLocked();
					}
					return true;
				}
			}
			return false;
		}

		const std::string& GetLanguage()
		{
			return s_CurrentCode;
		}

		u32 GetLanguageCount()
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			EnsureLoadedLocked();
			return (u32)s_Languages.size();
		}

		const char* GetLanguageCode(u32 index)
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			EnsureLoadedLocked();
			return index < (u32)s_Languages.size() ? s_Languages[index].code.c_str() : "";
		}

		const char* GetLanguageName(u32 index)
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			EnsureLoadedLocked();
			return index < (u32)s_Languages.size() ? s_Languages[index].name.c_str() : "";
		}

		const char* ItemName(const std::string& prefabName)
		{
			struct ItemNameEntry
			{
				const char* prefabName;
				LocID id;
			};

			static const ItemNameEntry itemNames[] =
			{
				{ "pickaxe", LOC_ID(Item_Pickaxe) },
				{ "axe", LOC_ID(Item_Axe) },
				{ "shovel", LOC_ID(Item_Shovel) },
				{ "wood", LOC_ID(Item_Wood) },
				{ "charcoal", LOC_ID(Item_Charcoal) },
				{ "dune buggy", LOC_ID(Item_DuneBuggy) },
				{ "row boat", LOC_ID(Item_RowBoat) },
				{ "headlamp", LOC_ID(Item_Headlamp) },
				{ "solar panel", LOC_ID(Item_SolarPanel) },
				{ "battery", LOC_ID(Item_Battery) },
				{ "lamp", LOC_ID(Item_Lamp) },
				{ "mineral deposit quartz", LOC_ID(Item_Quartz) },
				{ "mineral deposit aluminum", LOC_ID(Item_Aluminum) },
				{ "mineral deposit tin", LOC_ID(Item_Tin) },
				{ "mineral deposit olivine", LOC_ID(Item_Olivine) },
				{ "mineral deposit iron", LOC_ID(Item_Iron) },
				{ "mineral deposit silicon", LOC_ID(Item_Silicon) },
				{ "mineral deposit stone", LOC_ID(Item_Stone) },
			};

			for (const ItemNameEntry& entry : itemNames)
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
