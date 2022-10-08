#include "stdafx.hpp"

#include "GUID.hpp"

#include "Helpers.hpp"
#include "Scene/BaseScene.hpp"
#include "Scene/SceneManager.hpp"

namespace flex
{
	const GUID InvalidGUID = {};
	const GameObjectID InvalidGameObjectID = {};
	const EditorObjectID InvalidEditorObjectID = {};
	const GUID InvalidPrefabID = InvalidGUID;

	GUID::GUID()
	{
		Data1 = 0;
		Data2 = 0;
	}

	GUID::GUID(u64 data1, u64 data2) :
		Data1(data1),
		Data2(data2)
	{
	}

	GUID::GUID(const GUID& other)
	{
		Data1 = other.Data1;
		Data2 = other.Data2;
	}

	GUID::GUID(const GUID&& other)
	{
		Data1 = other.Data1;
		Data2 = other.Data2;
	}

	GUID& GUID::operator=(const GUID& other)
	{
		if (&other != this)
		{
			Data1 = other.Data1;
			Data2 = other.Data2;
		}
		return *this;
	}

	GUID& GUID::operator=(const GUID&& other)
	{
		if (&other != this)
		{
			Data1 = other.Data1;
			Data2 = other.Data2;
		}
		return *this;
	}

	bool GUID::operator!=(const GUID& rhs) const
	{
		return Data1 != rhs.Data1 && Data2 != rhs.Data2;
	}

	bool GUID::operator==(const GUID& rhs) const
	{
		// TODO: Compare perf of this with memcmp
		return Data1 == rhs.Data1 && Data2 == rhs.Data2;
	}

	bool GUID::operator<(const GUID& rhs) const
	{
		return Data2 < rhs.Data2 || (Data2 == rhs.Data2 && Data1 < rhs.Data1);
	}

	bool GUID::operator>(const GUID& rhs) const
	{
		return Data2 > rhs.Data2 || (Data2 == rhs.Data2 && Data1 > rhs.Data1);
	}

	bool GUID::IsValid() const
	{
		return memcmp(&Data1, &InvalidGameObjectID.Data1, sizeof(u64) + sizeof(u64)) != 0;
	}

	std::string GUID::ToString() const
	{
		char buffer[33];

		ToString(buffer);

		return std::string(buffer);
	}

	void GUID::ToString(char buffer[33]) const
	{
		// Write to buffer in reverse order (least significant to most significant)
		char* bufferPtr = buffer + 31;
		u64 data = Data1;
		for (u32 i = 0; i < 8; ++i)
		{
			u8 d = data & 0xFF;				 // Lowest byte of data
			u8 msb = DecimalToHex(d >> 4);   // Higher nibble
			u8 lsb = DecimalToHex(d & 0x0F); // Lower nibble
			data >>= 8; // /= 256
			*bufferPtr-- = lsb;
			*bufferPtr-- = msb;
		}

		data = Data2;
		for (u32 i = 0; i < 8; ++i)
		{
			u8 d = data & 0xFF;				 // Lowest byte of data
			u8 msb = DecimalToHex(d >> 4);	 // Higher nibble
			u8 lsb = DecimalToHex(d & 0x0F); // Lower nibble
			data >>= 8; // /= 256
			*bufferPtr-- = lsb;
			*bufferPtr-- = msb;
		}

		buffer[32] = 0; // Null terminator
	}

	GUID GUID::FromString(const std::string& str)
	{
		if (str.length() != 32)
		{
			return InvalidGUID;
		}

		const char* buffer = str.data();

#if DEBUG
		for (u32 i = 0; i < 32; ++i)
		{
			if (!((buffer[i] >= '0' && buffer[i] <= '9') ||
				(buffer[i] >= 'A' && buffer[i] <= 'F')))
			{
				return InvalidGUID;
			}
		}
#endif

		GUID result = {};

		// Read from string in reverse order (least significant to most significant)
		for (u32 i = 16; i >= 9; --i)
		{
			char msb = buffer[(i - 1) * 2];
			char lsb = buffer[(i - 1) * 2 + 1];

			u8 val = DecimalFromHex(msb) << 4 | DecimalFromHex(lsb);

			result.Data1 |= (u64)val << ((16 - i) * 8);
		}

		for (u32 i = 8; i >= 1; --i)
		{
			char msb = buffer[(i - 1) * 2];
			char lsb = buffer[(i - 1) * 2 + 1];

			u8 val = DecimalFromHex(msb) << 4 | DecimalFromHex(lsb);

			result.Data2 |= (u64)val << ((8 - i) * 8);
		}

		return result;
	}

	GameObjectID::GameObjectID() :
		GUID()
	{
	}

	GameObjectID::GameObjectID(u64 data1, u64 data2) :
		GUID(data1, data2)
	{
	}

	GameObjectID::GameObjectID(const GUID& guid) :
		GUID(guid)
	{
	}

	GameObject* GameObjectID::Get() const
	{
		if (!IsValid())
		{
			return nullptr;
		}
		BaseScene* scene = g_SceneManager->CurrentScene();
		if (scene != nullptr)
		{
			return scene->GetGameObject(*this);
		}
		return nullptr;
	}

	GameObjectID GameObjectID::FromString(const std::string& str)
	{
		GUID guid = GUID::FromString(str);
		return *(GameObjectID*)&guid;
	}

	EditorObjectID::EditorObjectID() :
		GUID()
	{
	}

	EditorObjectID::EditorObjectID(u64 data1, u64 data2) :
		GUID(data1, data2)
	{
	}

	EditorObjectID::EditorObjectID(const GUID& guid) :
		GUID(guid)
	{
	}

	GameObject* EditorObjectID::Get() const
	{
		if (!IsValid())
		{
			return nullptr;
		}
		BaseScene* scene = g_SceneManager->CurrentScene();
		if (scene != nullptr)
		{
			return scene->GetEditorObject(*this);
		}
		return nullptr;
	}

	EditorObjectID EditorObjectID::FromString(const std::string& str)
	{
		GUID guid = GUID::FromString(str);
		return *(EditorObjectID*)&guid;
	}
} // namespace flex
