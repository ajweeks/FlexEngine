#pragma once

namespace flex
{
	// Flat hash map from strings to T for the VM's compiler (the subset of std::map's interface it uses). clear() keeps
	// the storage (& the keys' string buffers), so recompiling doesn't allocate once it has grown big enough.
	// References to values stay valid until the next insertion.
	template<typename T>
	class StringMap
	{
	public:
		struct Entry
		{
			std::string first;
			T second;
		};

		using iterator = Entry*;

		iterator end() const { return nullptr; }

		u32 size() const { return m_Count; }
		bool empty() const { return m_Count == 0; }

		void clear()
		{
			m_Count = 0;
			std::fill(m_Buckets.begin(), m_Buckets.end(), -1);
		}

		iterator find(const std::string& key) { return find(key.c_str(), (u32)key.size()); }
		iterator find(const char* key) { return find(key, (u32)strlen(key)); }

		iterator find(const char* key, u32 length)
		{
			if (m_Count == 0)
			{
				return nullptr;
			}
			const u32 mask = (u32)m_Buckets.size() - 1;
			for (u32 bucket = Hash(key, length) & mask; ; bucket = (bucket + 1) & mask)
			{
				i32 index = m_Buckets[bucket];
				if (index == -1)
				{
					return nullptr;
				}
				Entry& entry = m_Entries[index];
				if (entry.first.size() == length && memcmp(entry.first.data(), key, length) == 0)
				{
					return &entry;
				}
			}
		}

		T& operator[](const std::string& key)
		{
			iterator iter = find(key);
			if (iter != nullptr)
			{
				return iter->second;
			}
			return Insert(key.c_str(), (u32)key.size(), T())->second;
		}

		// Does nothing when key is already present (like std::map)
		void emplace(const std::string& key, const T& value)
		{
			if (find(key) == nullptr)
			{
				Insert(key.c_str(), (u32)key.size(), value);
			}
		}

		// Entries in insertion order
		Entry* begin_entries() { return m_Entries.data(); }
		Entry* end_entries() { return m_Entries.data() + m_Count; }

	private:
		static u32 Hash(const char* key, u32 length)
		{
			// FNV-1a
			u32 hash = 2166136261u;
			for (u32 i = 0; i < length; ++i)
			{
				hash = (hash ^ (u8)key[i]) * 16777619u;
			}
			return hash;
		}

		Entry* Insert(const char* key, u32 length, const T& value)
		{
			if ((m_Count + 1) * 2 > (u32)m_Buckets.size())
			{
				Rehash(m_Buckets.empty() ? 32 : (u32)m_Buckets.size() * 2);
			}

			if (m_Count == (u32)m_Entries.size())
			{
				m_Entries.emplace_back();
			}
			Entry& entry = m_Entries[m_Count];
			entry.first.assign(key, length);
			entry.second = value;

			const u32 mask = (u32)m_Buckets.size() - 1;
			u32 bucket = Hash(key, length) & mask;
			while (m_Buckets[bucket] != -1)
			{
				bucket = (bucket + 1) & mask;
			}
			m_Buckets[bucket] = (i32)m_Count;
			++m_Count;
			return &entry;
		}

		void Rehash(u32 bucketCount)
		{
			m_Buckets.assign(bucketCount, -1);
			const u32 mask = bucketCount - 1;
			for (u32 i = 0; i < m_Count; ++i)
			{
				const std::string& key = m_Entries[i].first;
				u32 bucket = Hash(key.c_str(), (u32)key.size()) & mask;
				while (m_Buckets[bucket] != -1)
				{
					bucket = (bucket + 1) & mask;
				}
				m_Buckets[bucket] = (i32)i;
			}
		}

		std::vector<Entry> m_Entries;
		std::vector<i32> m_Buckets;
		u32 m_Count = 0;
	};
} // namespace flex
