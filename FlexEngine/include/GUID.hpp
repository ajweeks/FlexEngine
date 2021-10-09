#pragma once

#include <string>

#include "Types.hpp"

namespace flex
{
	struct GUID
	{
		GUID();
		GUID(u64 data1, u64 data2);

		GUID(const GUID& other);
		GUID(const GUID&& other);
		GUID& operator=(const GUID& other);
		GUID& operator=(const GUID&& other);

		bool operator!=(const GUID& rhs) const;
		bool operator==(const GUID& rhs) const;
		bool operator<(const GUID& rhs) const;
		bool operator>(const GUID& rhs) const;

		bool IsValid() const;

		std::string ToString() const;
		// Fills out buffer with this GUID's value in uppercase base 16 with a null terminator
		void ToString(char buffer[33]) const;

		static GUID FromString(const std::string& str);

		u64 Data1; // Stores least significant quad word
		u64 Data2; // Stores most significant quad word
	};

	struct GameObjectID : public GUID
	{
		GameObjectID();
		GameObjectID(u64 data1, u64 data2);

		GameObject* Get();

		static GameObjectID FromString(const std::string& str);
	};

	struct EditorObjectID : public GUID
	{
		EditorObjectID();
		EditorObjectID(u64 data1, u64 data2);

		GameObject* Get();

		static EditorObjectID FromString(const std::string& str);
	};

	using PrefabID = GUID;

	extern const GUID InvalidGUID;
	extern const GameObjectID InvalidGameObjectID;
	extern const EditorObjectID InvalidEditorObjectID;
	extern const GUID InvalidPrefabID;
} // namespace flex
