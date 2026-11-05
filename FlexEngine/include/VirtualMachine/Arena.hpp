#pragma once

namespace flex
{
	// Bump allocator for the VM's compiler: AST & IR nodes are allocated from it & freed all at once. Reset keeps the
	// memory, so recompiling with the same arena doesn't allocate (once it's grown big enough).
	class Arena
	{
	public:
		explicit Arena(u32 blockSize = 16 * 1024) :
			m_BlockSize(blockSize)
		{
		}

		~Arena()
		{
			Reset();
			for (Block& block : m_Blocks)
			{
				free(block.data);
			}
		}

		Arena(const Arena&) = delete;
		Arena(Arena&&) = delete;
		Arena& operator=(const Arena&) = delete;
		Arena& operator=(Arena&&) = delete;

		void* Alloc(size_t size, size_t alignment)
		{
			while (true)
			{
				if (m_CurrentBlock < (u32)m_Blocks.size())
				{
					Block& block = m_Blocks[m_CurrentBlock];
					size_t offset = (block.used + alignment - 1) & ~(alignment - 1);
					if (offset + size <= block.size)
					{
						block.used = offset + size;
						return block.data + offset;
					}
					if (m_CurrentBlock + 1 < (u32)m_Blocks.size() && m_Blocks[m_CurrentBlock + 1].size >= size + alignment)
					{
						++m_CurrentBlock;
						m_Blocks[m_CurrentBlock].used = 0;
						continue;
					}
				}

				// (New blocks are inserted after the current one, so following blocks are still reused after a reset)
				size_t blockSize = size + alignment > m_BlockSize ? size + alignment : m_BlockSize;
				Block block;
				block.data = (u8*)malloc(blockSize);
				block.size = blockSize;
				block.used = 0;
				u32 index = m_Blocks.empty() ? 0 : m_CurrentBlock + 1;
				m_Blocks.insert(m_Blocks.begin() + index, block);
				m_CurrentBlock = index;
			}
		}

		// Constructs a T in the arena. Its destructor runs on Reset (unless it's trivial).
		template<typename T, typename... Args>
		T* New(Args&&... args)
		{
			void* memory = Alloc(sizeof(T), alignof(T));
			T* result = new (memory) T(std::forward<Args>(args)...);
			if (!std::is_trivially_destructible<T>::value)
			{
				Destructor* destructor = (Destructor*)Alloc(sizeof(Destructor), alignof(Destructor));
				destructor->object = result;
				destructor->destroy = [](void* object) { ((T*)object)->~T(); };
				destructor->next = m_Destructors;
				m_Destructors = destructor;
			}
			return result;
		}

		// Copies str (null terminated) into the arena
		const char* NewString(const char* str, size_t length)
		{
			char* result = (char*)Alloc(length + 1, 1);
			memcpy(result, str, length);
			result[length] = '\0';
			return result;
		}

		// Destroys everything allocated so far, keeping the memory for reuse
		void Reset()
		{
			// (Newest first, like destroying a stack)
			for (Destructor* destructor = m_Destructors; destructor != nullptr; destructor = destructor->next)
			{
				destructor->destroy(destructor->object);
			}
			m_Destructors = nullptr;
			for (Block& block : m_Blocks)
			{
				block.used = 0;
			}
			m_CurrentBlock = 0;
		}

		size_t BytesUsed() const
		{
			size_t total = 0;
			for (const Block& block : m_Blocks)
			{
				total += block.used;
			}
			return total;
		}

	private:
		struct Block
		{
			u8* data;
			size_t size;
			size_t used;
		};

		struct Destructor
		{
			void* object;
			void(*destroy)(void*);
			Destructor* next;
		};

		std::vector<Block> m_Blocks;
		u32 m_CurrentBlock = 0;
		u32 m_BlockSize;
		Destructor* m_Destructors = nullptr;
	};
	// Growable array in an arena (for node lists: the arena frees it, so growing just leaves the old copy behind). Only
	// for trivially copyable types.
	template<typename T>
	class ArenaVector
	{
		static_assert(std::is_trivially_copyable<T>::value, "ArenaVector only holds trivially copyable types");

	public:
		ArenaVector() = default;

		explicit ArenaVector(Arena* arena) :
			m_Arena(arena)
		{
		}

		void push_back(const T& value)
		{
			if (m_Size == m_Capacity)
			{
				u32 newCapacity = m_Capacity == 0 ? 4 : m_Capacity * 2;
				T* newData = (T*)m_Arena->Alloc(sizeof(T) * newCapacity, alignof(T));
				if (m_Size > 0)
				{
					memcpy(newData, m_Data, sizeof(T) * m_Size);
				}
				m_Data = newData;
				m_Capacity = newCapacity;
			}
			m_Data[m_Size++] = value;
		}

		void emplace_back(const T& value) { push_back(value); }

		T* erase(T* iter)
		{
			u32 index = (u32)(iter - m_Data);
			if (index + 1 < m_Size)
			{
				memmove(m_Data + index, m_Data + index + 1, sizeof(T) * (m_Size - index - 1));
			}
			--m_Size;
			return m_Data + index;
		}

		void clear() { m_Size = 0; }

		u32 size() const { return m_Size; }
		bool empty() const { return m_Size == 0; }
		T& operator[](u32 index) { return m_Data[index]; }
		const T& operator[](u32 index) const { return m_Data[index]; }
		T& back() { return m_Data[m_Size - 1]; }
		T* begin() { return m_Data; }
		T* end() { return m_Data + m_Size; }
		const T* begin() const { return m_Data; }
		const T* end() const { return m_Data + m_Size; }

	private:
		Arena* m_Arena = nullptr;
		T* m_Data = nullptr;
		u32 m_Size = 0;
		u32 m_Capacity = 0;
	};
} // namespace flex
