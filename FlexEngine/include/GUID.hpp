#pragma once

#include <string>

#include "Types.hpp"

#undef GUID

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
		GameObjectID(const GUID& guid);

		GameObject* Get() const;

		static GameObjectID FromString(const std::string& str);
	};

	struct EditorObjectID : public GUID
	{
		EditorObjectID();
		EditorObjectID(u64 data1, u64 data2);
		EditorObjectID(const GUID& guid);

		GameObject* Get() const;

		static EditorObjectID FromString(const std::string& str);
	};

	using PrefabID = GUID;

	extern const GUID InvalidGUID;
	extern const GameObjectID InvalidGameObjectID;
	extern const EditorObjectID InvalidEditorObjectID;
	extern const GUID InvalidPrefabID;

	struct PrefabIDPair
	{
		// References a source prefab object (global ID)
		PrefabID m_PrefabID = InvalidPrefabID;
		// References an object within the source prefab (file-local ID)
		GameObjectID m_SubGameObjectID = InvalidGameObjectID;

		bool IsValid() const { return m_PrefabID.IsValid() && m_SubGameObjectID.IsValid(); }
		void Clear() { m_PrefabID = InvalidPrefabID; m_SubGameObjectID = InvalidGameObjectID; }
	};

	extern const PrefabIDPair InvalidPrefabIDPair;

} // namespace flex
