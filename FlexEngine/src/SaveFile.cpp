#include "stdafx.hpp"

#include "SaveFile.hpp"

#include "Helpers.hpp"
#include "JSONParser.hpp"
#include "Platform/Platform.hpp"
#include "Scene/TerrainGeneration.hpp"

#ifndef _WINDOWS
#include <stdio.h>
#include <unistd.h>
#endif

// Implemented in Helpers.cpp (stb_image_write). Only used to write: everything read from disk goes through the
// bounds checked decoder below instead.
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace flex
{
	namespace save
	{
		// -----------------------------------------------------------------------------------------
		// Writer & reader
		// -----------------------------------------------------------------------------------------

		void Writer::Bytes(const void* data, size_t size)
		{
			if (size > 0)
			{
				const u8* begin = (const u8*)data;
				bytes.insert(bytes.end(), begin, begin + size);
			}
		}

		void Writer::VarU64(u64 value)
		{
			while (value >= 0x80)
			{
				bytes.push_back((u8)(value | 0x80));
				value >>= 7;
			}
			bytes.push_back((u8)value);
		}

		void Writer::String(const std::string& str)
		{
			VarU64(str.size());
			Bytes(str.data(), str.size());
		}

		bool Reader::Bytes(void* out, size_t size)
		{
			if (m_bFailed || size > m_Size - m_Offset)
			{
				return Fail();
			}
			if (size > 0)
			{
				memcpy(out, m_Data + m_Offset, size);
				m_Offset += size;
			}
			return true;
		}

		bool Reader::VarU64(u64& outValue)
		{
			outValue = 0;
			for (u32 shift = 0; shift < 64; shift += 7)
			{
				u8 byte;
				if (!Read(byte))
				{
					return false;
				}
				if (shift == 63 && byte > 1)
				{
					return Fail(); // Overflow
				}
				outValue |= (u64)(byte & 0x7F) << shift;
				if ((byte & 0x80) == 0)
				{
					return true;
				}
			}
			return Fail();
		}

		bool Reader::Count(u32& outCount, size_t minBytesPerElement)
		{
			u64 count;
			if (!VarU64(count))
			{
				return false;
			}
			if (count > 0xFFFFFFFFull || (minBytesPerElement > 0 && count > Remaining() / minBytesPerElement))
			{
				return Fail();
			}
			outCount = (u32)count;
			return true;
		}

		bool Reader::String(std::string& outStr, size_t maxLength)
		{
			u64 length;
			if (!VarU64(length))
			{
				return false;
			}
			if (length > maxLength || length > Remaining())
			{
				return Fail();
			}
			outStr.assign((const char*)Current(), (size_t)length);
			m_Offset += (size_t)length;
			return true;
		}

		bool Reader::Skip(size_t size)
		{
			if (m_bFailed || size > m_Size - m_Offset)
			{
				return Fail();
			}
			m_Offset += size;
			return true;
		}

		// -----------------------------------------------------------------------------------------
		// Checksums & compression
		// -----------------------------------------------------------------------------------------

		u64 Ticks()
		{
			return (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		ms MsSince(u64 ticks)
		{
			return (ms)((double)(Ticks() - ticks) / 1e6);
		}

		u32 Crc32(const void* data, size_t size, u32 crc)
		{
			static u32 table[256];
			static bool bTableBuilt = [&]()
			{
				for (u32 i = 0; i < 256; ++i)
				{
					u32 c = i;
					for (u32 k = 0; k < 8; ++k)
					{
						c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
					}
					table[i] = c;
				}
				return true;
			}();
			FLEX_UNUSED(bTableBuilt);

			const u8* bytes = (const u8*)data;
			crc = ~crc;
			for (size_t i = 0; i < size; ++i)
			{
				crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
			}
			return ~crc;
		}

		bool Compress(const u8* data, size_t size, std::vector<u8>& outCompressed)
		{
			outCompressed.clear();
			if (size == 0 || size > MAX_SECTION_SIZE)
			{
				return false;
			}
			int compressedSize = 0;
			// (The lowest quality: the fastest, & barely larger)
			unsigned char* compressed = stbi_zlib_compress((unsigned char*)data, (int)size, &compressedSize, 5);
			if (compressed == nullptr)
			{
				return false;
			}
			outCompressed.assign(compressed, compressed + compressedSize);
			free(compressed);
			return compressedSize > 0;
		}

		// A strictly bounds checked inflate (zlib stream; stored, fixed & dynamic Huffman blocks), after Mark Adler's
		// puff.c. Never reads past the input or writes past rawSize, & rejects malformed code sets.
		namespace inflate
		{
			struct State
			{
				const u8* in;
				size_t inSize;
				size_t inPos;
				u32 bitBuffer;
				u32 bitCount;
				u8* out;
				size_t outSize;
				size_t outPos;
				bool bError;
			};

			static u32 Bits(State& s, u32 need)
			{
				u64 value = s.bitBuffer;
				while (s.bitCount < need)
				{
					if (s.inPos >= s.inSize)
					{
						s.bError = true;
						return 0;
					}
					value |= (u64)s.in[s.inPos++] << s.bitCount;
					s.bitCount += 8;
				}
				s.bitBuffer = (u32)(value >> need);
				s.bitCount -= need;
				return (u32)(value & ((1ull << need) - 1));
			}

			static const u32 MAX_BITS = 15;
			static const u32 MAX_LENGTH_CODES = 286;
			static const u32 MAX_DIST_CODES = 30;
			static const u32 FIXED_LENGTH_CODES = 288;

			// Codes up to this long are decoded with one table lookup
			static const u32 FAST_BITS = 9;

			struct Huffman
			{
				u16 count[MAX_BITS + 1];
				u16 symbol[FIXED_LENGTH_CODES];
				u16 fast[1 << FAST_BITS]; // (length << 9) | symbol, indexed by the next FAST_BITS input bits (0: not a short code)
			};

			// Returns 0 for a complete code, > 0 if incomplete, < 0 if over-subscribed
			static i32 Construct(Huffman& h, const u16* lengths, u32 n)
			{
				memset(h.count, 0, sizeof(h.count));
				memset(h.fast, 0, sizeof(h.fast));
				for (u32 symbol = 0; symbol < n; ++symbol)
				{
					if (lengths[symbol] > MAX_BITS)
					{
						return -1;
					}
					h.count[lengths[symbol]]++;
				}
				if (h.count[0] == n)
				{
					return 0;
				}

				i32 left = 1;
				for (u32 len = 1; len <= MAX_BITS; ++len)
				{
					left <<= 1;
					left -= h.count[len];
					if (left < 0)
					{
						return left;
					}
				}

				u16 offsets[MAX_BITS + 1];
				offsets[1] = 0;
				for (u32 len = 1; len < MAX_BITS; ++len)
				{
					offsets[len + 1] = offsets[len] + h.count[len];
				}
				for (u32 symbol = 0; symbol < n; ++symbol)
				{
					if (lengths[symbol] != 0)
					{
						h.symbol[offsets[lengths[symbol]]++] = (u16)symbol;
					}
				}

				// Canonical codes are assigned in symbol order within each length. Deflate sends them most significant
				// bit first, so the table is indexed by their reversed bits.
				u32 code = 0;
				u32 index = 0;
				for (u32 len = 1; len <= MAX_BITS; ++len)
				{
					for (u32 i = 0; i < h.count[len]; ++i, ++code)
					{
						const u16 symbol = h.symbol[index++];
						if (len <= FAST_BITS)
						{
							u32 reversed = 0;
							for (u32 bit = 0; bit < len; ++bit)
							{
								reversed |= ((code >> bit) & 1) << (len - 1 - bit);
							}
							for (u32 fill = reversed; fill < (1u << FAST_BITS); fill += 1u << len)
							{
								h.fast[fill] = (u16)((len << 9) | symbol);
							}
						}
					}
					code <<= 1;
				}
				return left;
			}

			static i32 Decode(State& s, const Huffman& h)
			{
				// Fast path: enough bits buffered for a table lookup
				while (s.bitCount <= 24 && s.inPos < s.inSize)
				{
					s.bitBuffer |= (u32)s.in[s.inPos++] << s.bitCount;
					s.bitCount += 8;
				}
				if (s.bitCount >= FAST_BITS)
				{
					const u16 entry = h.fast[s.bitBuffer & ((1u << FAST_BITS) - 1)];
					if (entry != 0)
					{
						const u32 len = entry >> 9;
						s.bitBuffer >>= len;
						s.bitCount -= len;
						return entry & 511;
					}
				}

				// Long codes (& the end of the input) a bit at a time
				i32 code = 0;
				i32 first = 0;
				i32 index = 0;
				for (u32 len = 1; len <= MAX_BITS; ++len)
				{
					code |= (i32)Bits(s, 1);
					if (s.bError)
					{
						return -1;
					}
					const i32 count = h.count[len];
					if (code - count < first)
					{
						return h.symbol[index + (code - first)];
					}
					index += count;
					first += count;
					first <<= 1;
					code <<= 1;
				}
				return -1;
			}

			static bool Stored(State& s)
			{
				// Skip to the next byte boundary, giving back any whole bytes already buffered
				s.inPos -= s.bitCount / 8;
				s.bitBuffer = 0;
				s.bitCount = 0;
				if (s.inSize - s.inPos < 4)
				{
					return false;
				}
				const u32 length = (u32)s.in[s.inPos] | ((u32)s.in[s.inPos + 1] << 8);
				const u32 lengthComplement = (u32)s.in[s.inPos + 2] | ((u32)s.in[s.inPos + 3] << 8);
				s.inPos += 4;
				if (length != (~lengthComplement & 0xFFFF) || length > s.inSize - s.inPos || length > s.outSize - s.outPos)
				{
					return false;
				}
				memcpy(s.out + s.outPos, s.in + s.inPos, length);
				s.inPos += length;
				s.outPos += length;
				return true;
			}

			static bool Codes(State& s, const Huffman& lengthCode, const Huffman& distCode)
			{
				static const u16 lengthBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
				static const u16 lengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
				static const u16 distBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
				static const u16 distExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

				for (;;)
				{
					i32 symbol = Decode(s, lengthCode);
					if (symbol < 0)
					{
						return false;
					}
					if (symbol < 256)
					{
						if (s.outPos >= s.outSize)
						{
							return false;
						}
						s.out[s.outPos++] = (u8)symbol;
					}
					else if (symbol == 256)
					{
						return true;
					}
					else
					{
						symbol -= 257;
						if (symbol >= 29)
						{
							return false;
						}
						const size_t length = lengthBase[symbol] + Bits(s, lengthExtra[symbol]);
						symbol = Decode(s, distCode);
						if (symbol < 0 || symbol >= 30)
						{
							return false;
						}
						const size_t dist = distBase[symbol] + Bits(s, distExtra[symbol]);
						if (s.bError || dist > s.outPos || length > s.outSize - s.outPos)
						{
							return false;
						}
						for (size_t i = 0; i < length; ++i, ++s.outPos)
						{
							s.out[s.outPos] = s.out[s.outPos - dist];
						}
					}
				}
			}

			static bool Fixed(State& s)
			{
				static Huffman lengthCode, distCode;
				static bool bBuilt = []()
				{
					u16 lengths[FIXED_LENGTH_CODES];
					u32 symbol = 0;
					for (; symbol < 144; ++symbol) lengths[symbol] = 8;
					for (; symbol < 256; ++symbol) lengths[symbol] = 9;
					for (; symbol < 280; ++symbol) lengths[symbol] = 7;
					for (; symbol < FIXED_LENGTH_CODES; ++symbol) lengths[symbol] = 8;
					Construct(lengthCode, lengths, FIXED_LENGTH_CODES);
					for (symbol = 0; symbol < MAX_DIST_CODES; ++symbol) lengths[symbol] = 5;
					Construct(distCode, lengths, MAX_DIST_CODES);
					return true;
				}();
				FLEX_UNUSED(bBuilt);
				return Codes(s, lengthCode, distCode);
			}

			static bool Dynamic(State& s)
			{
				static const u8 order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

				const u32 lengthCount = Bits(s, 5) + 257;
				const u32 distCount = Bits(s, 5) + 1;
				const u32 codeCount = Bits(s, 4) + 4;
				if (s.bError || lengthCount > MAX_LENGTH_CODES || distCount > MAX_DIST_CODES)
				{
					return false;
				}

				u16 lengths[MAX_LENGTH_CODES + MAX_DIST_CODES] = {};
				for (u32 i = 0; i < codeCount; ++i)
				{
					lengths[order[i]] = (u16)Bits(s, 3);
				}
				Huffman lengthCode, distCode;
				if (s.bError || Construct(lengthCode, lengths, 19) != 0)
				{
					return false;
				}

				u32 index = 0;
				while (index < lengthCount + distCount)
				{
					i32 symbol = Decode(s, lengthCode);
					if (symbol < 0)
					{
						return false;
					}
					if (symbol < 16)
					{
						lengths[index++] = (u16)symbol;
					}
					else
					{
						u16 length = 0;
						u32 repeat;
						if (symbol == 16)
						{
							if (index == 0)
							{
								return false;
							}
							length = lengths[index - 1];
							repeat = 3 + Bits(s, 2);
						}
						else if (symbol == 17)
						{
							repeat = 3 + Bits(s, 3);
						}
						else
						{
							repeat = 11 + Bits(s, 7);
						}
						if (s.bError || index + repeat > lengthCount + distCount)
						{
							return false;
						}
						while (repeat-- > 0)
						{
							lengths[index++] = length;
						}
					}
				}

				// There must be an end of block code
				if (lengths[256] == 0)
				{
					return false;
				}
				i32 err = Construct(lengthCode, lengths, lengthCount);
				if (err < 0 || (err > 0 && lengthCount - lengthCode.count[0] != 1))
				{
					return false;
				}
				err = Construct(distCode, lengths + lengthCount, distCount);
				if (err < 0 || (err > 0 && distCount - distCode.count[0] != 1))
				{
					return false;
				}
				return Codes(s, lengthCode, distCode);
			}

			static bool ZlibInflate(const u8* in, size_t inSize, u8* out, size_t outSize)
			{
				if (inSize < 6)
				{
					return false;
				}
				const u32 cmf = in[0];
				const u32 flg = in[1];
				if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20) != 0)
				{
					return false;
				}

				State s = {};
				s.in = in;
				s.inSize = inSize - 4; // Adler-32 trailer
				s.inPos = 2;
				s.out = out;
				s.outSize = outSize;

				u32 last;
				do
				{
					last = Bits(s, 1);
					const u32 type = Bits(s, 2);
					if (s.bError)
					{
						return false;
					}
					bool bOk;
					switch (type)
					{
					case 0: bOk = Stored(s); break;
					case 1: bOk = Fixed(s); break;
					case 2: bOk = Dynamic(s); break;
					default: bOk = false; break;
					}
					if (!bOk || s.bError)
					{
						return false;
					}
				} while (last == 0);

				if (s.outPos != outSize)
				{
					return false;
				}

				u32 a = 1, b = 0;
				for (size_t i = 0; i < outSize; ++i)
				{
					a = (a + out[i]) % 65521;
					b = (b + a) % 65521;
				}
				const u8* trailer = in + inSize - 4;
				const u32 adler = ((u32)trailer[0] << 24) | ((u32)trailer[1] << 16) | ((u32)trailer[2] << 8) | (u32)trailer[3];
				return adler == ((b << 16) | a);
			}
		} // namespace inflate

		bool Decompress(const u8* data, size_t size, size_t rawSize, std::vector<u8>& outRaw)
		{
			outRaw.clear();
			if (data == nullptr || rawSize > MAX_SECTION_SIZE)
			{
				return false;
			}
			// Deflate can't expand data by more than ~1032x, so a tiny stream can't claim a huge size
			if (rawSize / 1032 > size)
			{
				return false;
			}
			outRaw.resize(rawSize);
			if (!inflate::ZlibInflate(data, size, outRaw.data(), rawSize))
			{
				outRaw.clear();
				return false;
			}
			return true;
		}

		void ShuffleBytes(const u8* data, size_t elementCount, size_t elementSize, u8* out)
		{
			for (size_t k = 0; k < elementSize; ++k)
			{
				for (size_t i = 0; i < elementCount; ++i)
				{
					out[k * elementCount + i] = data[i * elementSize + k];
				}
			}
		}

		void UnshuffleBytes(const u8* data, size_t elementCount, size_t elementSize, u8* out)
		{
			for (size_t k = 0; k < elementSize; ++k)
			{
				for (size_t i = 0; i < elementCount; ++i)
				{
					out[i * elementSize + k] = data[k * elementCount + i];
				}
			}
		}

		// -----------------------------------------------------------------------------------------
		// Files
		// -----------------------------------------------------------------------------------------

		bool ReadFileBytes(const std::string& filePath, std::vector<u8>& outBytes, size_t maxSize)
		{
			outBytes.clear();
			std::ifstream file(filePath, std::ios::binary | std::ios::ate);
			if (!file)
			{
				return false;
			}
			const std::streamoff size = (std::streamoff)file.tellg();
			if (size < 0 || (u64)size > (u64)maxSize)
			{
				return false;
			}
			outBytes.resize((size_t)size);
			file.seekg(0);
			if (size > 0 && !file.read((char*)outBytes.data(), (std::streamsize)size))
			{
				outBytes.clear();
				return false;
			}
			return true;
		}

		bool WriteFileAtomic(const std::string& filePath, const void* data, size_t size, bool bKeepBackup)
		{
			const std::string tempPath = filePath + ".tmp";
			const std::string backupPath = filePath + ".bak";
			const bool bExists = FileExists(filePath);

#ifdef _WINDOWS
			HANDLE file = CreateFileA(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			bool bWritten = true;
			const u8* bytes = (const u8*)data;
			size_t offset = 0;
			while (bWritten && offset < size)
			{
				const DWORD chunk = (DWORD)glm::min(size - offset, (size_t)(64u * 1024u * 1024u));
				DWORD written = 0;
				bWritten = ::WriteFile(file, bytes + offset, chunk, &written, nullptr) != 0 && written == chunk;
				offset += chunk;
			}
			// So the new file is on disk before it replaces the old one
			bWritten = bWritten && FlushFileBuffers(file) != 0;
			CloseHandle(file);
			if (!bWritten)
			{
				DeleteFileA(tempPath.c_str());
				return false;
			}

			if (bExists && bKeepBackup)
			{
				// Atomically swaps in the new file, keeping the old one as the backup
				if (ReplaceFileA(filePath.c_str(), tempPath.c_str(), backupPath.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != 0)
				{
					return true;
				}
			}
			return MoveFileExA(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
			FILE* file = fopen(tempPath.c_str(), "wb");
			if (file == nullptr)
			{
				return false;
			}
			bool bWritten = size == 0 || fwrite(data, 1, size, file) == size;
			bWritten = bWritten && fflush(file) == 0 && fsync(fileno(file)) == 0;
			bWritten = (fclose(file) == 0) && bWritten;
			if (!bWritten)
			{
				remove(tempPath.c_str());
				return false;
			}

			if (bExists && bKeepBackup)
			{
				// A hard link keeps the old file in place until the rename below replaces it
				remove(backupPath.c_str());
				if (link(filePath.c_str(), backupPath.c_str()) != 0)
				{
					std::vector<u8> oldBytes;
					if (ReadFileBytes(filePath, oldBytes))
					{
						FILE* backup = fopen(backupPath.c_str(), "wb");
						if (backup != nullptr)
						{
							fwrite(oldBytes.data(), 1, oldBytes.size(), backup);
							fclose(backup);
						}
					}
				}
			}
			return rename(tempPath.c_str(), filePath.c_str()) == 0;
#endif
		}

		// -----------------------------------------------------------------------------------------
		// Container
		// -----------------------------------------------------------------------------------------

		static const size_t HEADER_SIZE = 24;
		static const size_t SECTION_HEADER_SIZE = 20;

		void FileWriter::AddSection(u32 tag, const std::vector<u8>& raw, u32 flags, bool bCompress)
		{
			Section section = {};
			section.tag = tag;
			section.flags = flags & ~(u32)SECTION_ZLIB;
			section.rawSize = (u32)raw.size();

			std::vector<u8> compressed;
			if (bCompress && raw.size() >= 64 && Compress(raw.data(), raw.size(), compressed) && compressed.size() <= raw.size() - raw.size() / 8)
			{
				section.flags |= SECTION_ZLIB;
				section.stored = std::move(compressed);
			}
			else
			{
				section.stored = raw;
			}
			section.rawCrc = Crc32(section.stored.data(), section.stored.size());
			m_Sections.push_back(std::move(section));
		}

		void FileWriter::AddJSONSection(u32 tag, const JSONObject& fields, bool bCompress)
		{
			std::vector<u8> bytes;
			EncodeJSON(fields, bytes);
			AddSection(tag, bytes, SECTION_JSON_FIELDS, bCompress);
		}

		std::vector<u8> FileWriter::Build() const
		{
			Writer writer;
			size_t totalSize = HEADER_SIZE;
			for (const Section& section : m_Sections)
			{
				totalSize += SECTION_HEADER_SIZE + section.stored.size();
			}
			writer.bytes.reserve(totalSize);

			writer.Write(CONTAINER_MAGIC);
			writer.Write(CONTAINER_VERSION);
			writer.Write((u16)0); // Header flags
			writer.Write(m_FileType);
			writer.Write(m_FileVersion);
			writer.Write((u32)m_Sections.size());
			writer.Write(Crc32(writer.bytes.data(), writer.bytes.size()));

			for (const Section& section : m_Sections)
			{
				writer.Write(section.tag);
				writer.Write(section.flags);
				writer.Write((u32)section.stored.size());
				writer.Write(section.rawSize);
				writer.Write(section.rawCrc);
				writer.Bytes(section.stored.data(), section.stored.size());
			}
			return std::move(writer.bytes);
		}

		bool FileWriter::Save(const std::string& filePath, bool bKeepBackup, size_t* outSize) const
		{
			const std::vector<u8> bytes = Build();
			if (outSize != nullptr)
			{
				*outSize = bytes.size();
			}
			return WriteFileAtomic(filePath, bytes.data(), bytes.size(), bKeepBackup);
		}

		const char* LoadResultToString(LoadResult result)
		{
			switch (result)
			{
			case LoadResult::OK: return "ok";
			case LoadResult::MISSING: return "missing";
			case LoadResult::NOT_CONTAINER: return "not a binary save";
			case LoadResult::WRONG_TYPE: return "wrong file type";
			case LoadResult::UNSUPPORTED_VERSION: return "unsupported version";
			case LoadResult::CORRUPT: return "corrupt";
			default: return "unknown";
			}
		}

		static LoadResult ParseHeader(const u8* data, size_t size, u32 expectedType, u32& outFileVersion, u32& outSectionCount)
		{
			Reader reader(data, size);
			u32 magic = 0, fileType = 0, headerCrc = 0;
			u16 containerVersion = 0, headerFlags = 0;
			if (!reader.Read(magic))
			{
				return LoadResult::CORRUPT;
			}
			if (magic != CONTAINER_MAGIC)
			{
				return LoadResult::NOT_CONTAINER;
			}
			if (!reader.Read(containerVersion) || !reader.Read(headerFlags) || !reader.Read(fileType) || !reader.Read(outFileVersion) ||
				!reader.Read(outSectionCount) || !reader.Read(headerCrc))
			{
				return LoadResult::CORRUPT;
			}
			if (headerCrc != Crc32(data, HEADER_SIZE - sizeof(u32)))
			{
				return LoadResult::CORRUPT;
			}
			if (containerVersion != CONTAINER_VERSION)
			{
				return LoadResult::UNSUPPORTED_VERSION;
			}
			if (fileType != expectedType)
			{
				return LoadResult::WRONG_TYPE;
			}
			if (outSectionCount > MAX_SECTION_COUNT)
			{
				return LoadResult::CORRUPT;
			}
			return LoadResult::OK;
		}

		LoadResult FileReader::Parse(const u8* data, size_t size, u32 expectedType)
		{
			m_Sections.clear();
			m_FileVersion = 0;

			u32 sectionCount = 0;
			const LoadResult headerResult = ParseHeader(data, size, expectedType, m_FileVersion, sectionCount);
			if (headerResult != LoadResult::OK)
			{
				return headerResult;
			}

			Reader reader(data, size);
			reader.Skip(HEADER_SIZE);
			for (u32 i = 0; i < sectionCount; ++i)
			{
				Section section = {};
				u32 storedSize = 0;
				if (!reader.Read(section.tag) || !reader.Read(section.flags) || !reader.Read(storedSize) || !reader.Read(section.rawSize) ||
					!reader.Read(section.rawCrc) || storedSize > reader.Remaining() || section.rawSize > MAX_SECTION_SIZE)
				{
					m_Sections.clear();
					return LoadResult::CORRUPT;
				}
				section.stored.assign(reader.Current(), reader.Current() + storedSize);
				reader.Skip(storedSize);
				m_Sections.push_back(std::move(section));
			}
			if (!reader.AtEnd())
			{
				// Trailing bytes: something's wrong with the section table
				m_Sections.clear();
				return LoadResult::CORRUPT;
			}
			return LoadResult::OK;
		}

		LoadResult FileReader::Load(const std::string& filePath, u32 expectedType, const std::vector<u32>* onlyTags)
		{
			m_Sections.clear();
			m_FileVersion = 0;

			if (onlyTags == nullptr)
			{
				std::vector<u8> bytes;
				if (!ReadFileBytes(filePath, bytes))
				{
					return FileExists(filePath) ? LoadResult::CORRUPT : LoadResult::MISSING;
				}
				return Parse(bytes.data(), bytes.size(), expectedType);
			}

			// Only the requested sections are read; others are seeked past
			std::ifstream file(filePath, std::ios::binary | std::ios::ate);
			if (!file)
			{
				return LoadResult::MISSING;
			}
			const std::streamoff fileSize = (std::streamoff)file.tellg();
			file.seekg(0);
			u8 header[HEADER_SIZE];
			if (fileSize < (std::streamoff)HEADER_SIZE || !file.read((char*)header, HEADER_SIZE))
			{
				return fileSize >= 4 ? LoadResult::CORRUPT : LoadResult::NOT_CONTAINER;
			}
			u32 sectionCount = 0;
			const LoadResult headerResult = ParseHeader(header, HEADER_SIZE, expectedType, m_FileVersion, sectionCount);
			if (headerResult != LoadResult::OK)
			{
				return headerResult;
			}

			u64 offset = HEADER_SIZE;
			for (u32 i = 0; i < sectionCount; ++i)
			{
				u8 sectionHeader[SECTION_HEADER_SIZE];
				if ((u64)fileSize - offset < SECTION_HEADER_SIZE || !file.read((char*)sectionHeader, SECTION_HEADER_SIZE))
				{
					m_Sections.clear();
					return LoadResult::CORRUPT;
				}
				offset += SECTION_HEADER_SIZE;
				Reader reader(sectionHeader, SECTION_HEADER_SIZE);
				Section section = {};
				u32 storedSize = 0;
				reader.Read(section.tag);
				reader.Read(section.flags);
				reader.Read(storedSize);
				reader.Read(section.rawSize);
				reader.Read(section.rawCrc);
				if ((u64)storedSize > (u64)fileSize - offset || section.rawSize > MAX_SECTION_SIZE)
				{
					m_Sections.clear();
					return LoadResult::CORRUPT;
				}
				if (Contains(*onlyTags, section.tag))
				{
					section.stored.resize(storedSize);
					if (storedSize > 0 && !file.read((char*)section.stored.data(), storedSize))
					{
						m_Sections.clear();
						return LoadResult::CORRUPT;
					}
					m_Sections.push_back(std::move(section));
				}
				else
				{
					file.seekg((std::streamoff)storedSize, std::ios::cur);
				}
				offset += storedSize;
			}
			if (offset != (u64)fileSize)
			{
				m_Sections.clear();
				return LoadResult::CORRUPT;
			}
			return LoadResult::OK;
		}

		bool FileReader::HasSection(u32 tag) const
		{
			for (const Section& section : m_Sections)
			{
				if (section.tag == tag)
				{
					return true;
				}
			}
			return false;
		}

		u32 FileReader::SectionFlags(u32 tag) const
		{
			for (const Section& section : m_Sections)
			{
				if (section.tag == tag)
				{
					return section.flags;
				}
			}
			return 0;
		}

		std::vector<u32> FileReader::Tags() const
		{
			std::vector<u32> tags;
			for (const Section& section : m_Sections)
			{
				tags.push_back(section.tag);
			}
			return tags;
		}

		bool FileReader::GetSection(const Section& section, std::vector<u8>& outRaw) const
		{
			outRaw.clear();
			if ((section.flags & SECTION_REQUIRED_FLAGS_MASK & ~(u32)SECTION_KNOWN_FLAGS) != 0)
			{
				return false;
			}
			if (Crc32(section.stored.data(), section.stored.size()) != section.rawCrc)
			{
				return false;
			}
			if (section.flags & SECTION_ZLIB)
			{
				return Decompress(section.stored.data(), section.stored.size(), section.rawSize, outRaw);
			}
			if (section.stored.size() != section.rawSize)
			{
				return false;
			}
			outRaw = section.stored;
			return true;
		}

		bool FileReader::GetSection(u32 tag, std::vector<u8>& outRaw) const
		{
			// The first section with a tag wins
			for (const Section& section : m_Sections)
			{
				if (section.tag == tag)
				{
					return GetSection(section, outRaw);
				}
			}
			outRaw.clear();
			return false;
		}

		bool FileReader::GetJSONFields(JSONObject& outObject) const
		{
			bool bAllRead = true;
			std::vector<u8> raw;
			for (const Section& section : m_Sections)
			{
				if ((section.flags & SECTION_JSON_FIELDS) == 0)
				{
					continue;
				}
				JSONObject fields;
				if (!GetSection(section, raw) || !DecodeJSON(raw.data(), raw.size(), fields))
				{
					bAllRead = false;
					continue;
				}
				for (JSONField& field : fields.fields)
				{
					outObject.fields.push_back(std::move(field));
				}
			}
			return bAllRead;
		}

		// -----------------------------------------------------------------------------------------
		// Binary JSON
		// -----------------------------------------------------------------------------------------
		//
		// u8 version, string table (count, strings), root object
		// object: count, then (label string index, value) per field
		// value: u8 tag, then its payload
		//
		// Values are written as the text format would represent them, & decoded the way JSONParser would read that
		// text back (e.g. numbers become UINT/INT/ULONG/LONG by sign & magnitude, quaternions become VEC4, strings
		// containing commas become vectors), so code that loads JSON behaves the same either way.

		static const u8 BJSON_VERSION = 1;
		static const u32 BJSON_MAX_DEPTH = 64;
		// Decoded values take far more memory than their encoding, so damaged data can't claim more than this
		static const u32 BJSON_MAX_VALUES = 4u * 1024u * 1024u;
		static const size_t BJSON_MAX_SIZE = 64u * 1024u * 1024u;

		enum BJSONTag : u8
		{
			BJ_STRING = 1,		// String table index
			BJ_GUID_STRING,		// 16 bytes, decoded to its 32 character string (GUIDs are saved as strings)
			BJ_POS_INT,			// varint
			BJ_NEG_INT,			// varint of -(value + 1)
			BJ_FLOAT,			// f32
			BJ_FALSE,
			BJ_TRUE,
			BJ_VEC2,			// 2x f32
			BJ_VEC3,			// 3x f32
			BJ_VEC4,			// 4x f32
			BJ_OBJECT,
			BJ_OBJECT_ARRAY,	// count, objects
			BJ_FIELD_ARRAY,		// count, values (labels aren't kept, as with text)
		};

		struct BJSONEncoder
		{
			Writer body;
			std::vector<const std::string*> strings;
			std::unordered_map<std::string, u32> stringIndices;

			void StringIndex(const std::string& str)
			{
				auto iter = stringIndices.find(str);
				if (iter == stringIndices.end())
				{
					iter = stringIndices.emplace(str, (u32)strings.size()).first;
					strings.push_back(&iter->first);
				}
				body.VarU64(iter->second);
			}

			static bool IsGUIDString(const std::string& str, GUID& outGUID)
			{
				if (str.size() != 32)
				{
					return false;
				}
				outGUID = GUID::FromString(str);
				char buffer[33];
				outGUID.ToString(buffer);
				return memcmp(buffer, str.data(), 32) == 0;
			}

			void Integer(bool bNegative, u64 magnitude)
			{
				// magnitude is -(value + 1) for negatives
				body.Write(bNegative ? BJ_NEG_INT : BJ_POS_INT);
				body.VarU64(magnitude);
			}

			void SignedInteger(i64 value)
			{
				if (value < 0)
				{
					Integer(true, (u64)(-(value + 1)));
				}
				else
				{
					Integer(false, (u64)value);
				}
			}

			void Floats(BJSONTag tag, const real* values, u32 count)
			{
				body.Write(tag);
				for (u32 i = 0; i < count; ++i)
				{
					body.Write(values[i]);
				}
			}

			// Returns false for values text can't represent (which are skipped)
			bool Value(const JSONValue& value)
			{
				switch (value.type)
				{
				case ValueType::STRING:
				{
					GUID guid;
					if (IsGUIDString(value.strValue, guid))
					{
						body.Write(BJ_GUID_STRING);
						body.Write(guid.Data1);
						body.Write(guid.Data2);
					}
					else
					{
						body.Write(BJ_STRING);
						StringIndex(value.strValue);
					}
				} return true;
				case ValueType::INT: SignedInteger(value.intValue); return true;
				case ValueType::UINT: Integer(false, value.uintValue); return true;
				case ValueType::LONG: SignedInteger(value.longValue); return true;
				case ValueType::ULONG: Integer(false, value.ulongValue); return true;
				case ValueType::FLOAT:
				{
					real f = value.floatValue;
					if (!std::isfinite(f))
					{
						// (As text saves write NaN & inf)
						f = 0.0f;
					}
					if (value.floatPrecision == 0 && std::abs(f) < 9.0e18f)
					{
						// Written without a decimal point, so read back as an integer
						SignedInteger((i64)std::nearbyint(f));
						return true;
					}
					Floats(BJ_FLOAT, &f, 1);
				} return true;
				case ValueType::BOOL:
					body.Write(value.uintValue != 0 ? BJ_TRUE : BJ_FALSE);
					return true;
				case ValueType::VEC2: Floats(BJ_VEC2, &value.vecValue.x, 2); return true;
				case ValueType::VEC3: Floats(BJ_VEC3, &value.vecValue.x, 3); return true;
				case ValueType::VEC4:
				case ValueType::QUAT:
					Floats(BJ_VEC4, &value.vecValue.x, 4);
					return true;
				case ValueType::MAT4:
				{
					// Text keeps matrices as strings, which their loaders parse
					std::string str = JSONField("", value).ToString(0);
					if (str.size() >= 2 && str.front() == '\"' && str.back() == '\"')
					{
						str = str.substr(1, str.size() - 2);
					}
					body.Write(BJ_STRING);
					StringIndex(str);
				} return true;
				case ValueType::GUID:
					body.Write(BJ_GUID_STRING);
					body.Write(value.guidValue.Data1);
					body.Write(value.guidValue.Data2);
					return true;
				case ValueType::OBJECT:
					body.Write(BJ_OBJECT);
					Object(value.objectValue);
					return true;
				case ValueType::OBJECT_ARRAY:
					if (value.objectArrayValue.empty())
					{
						// "[]" is read back as an empty field array
						body.Write(BJ_FIELD_ARRAY);
						body.VarU64(0);
						return true;
					}
					body.Write(BJ_OBJECT_ARRAY);
					body.VarU64(value.objectArrayValue.size());
					for (const JSONObject& object : value.objectArrayValue)
					{
						Object(object);
					}
					return true;
				case ValueType::FIELD_ARRAY:
				{
					u32 count = 0;
					for (const JSONField& field : value.fieldArrayValue)
					{
						count += IsWritable(field.value.type) ? 1 : 0;
					}
					body.Write(BJ_FIELD_ARRAY);
					body.VarU64(count);
					for (const JSONField& field : value.fieldArrayValue)
					{
						if (IsWritable(field.value.type))
						{
							Value(field.value);
						}
					}
				} return true;
				default:
					return false;
				}
			}

			static bool IsWritable(ValueType type)
			{
				return type != ValueType::UNINITIALIZED && type != ValueType::FIELD_ENTRY;
			}

			void Object(const JSONObject& object)
			{
				u32 count = 0;
				for (const JSONField& field : object.fields)
				{
					count += IsWritable(field.value.type) ? 1 : 0;
				}
				body.VarU64(count);
				for (const JSONField& field : object.fields)
				{
					if (IsWritable(field.value.type))
					{
						StringIndex(field.label);
						Value(field.value);
					}
				}
			}
		};

		void EncodeJSON(const JSONObject& object, std::vector<u8>& outBytes)
		{
			BJSONEncoder encoder;
			encoder.Object(object);

			Writer writer;
			writer.Write(BJSON_VERSION);
			writer.VarU64(encoder.strings.size());
			for (const std::string* str : encoder.strings)
			{
				writer.String(*str);
			}
			writer.Bytes(encoder.body.bytes.data(), encoder.body.bytes.size());
			outBytes = std::move(writer.bytes);
		}

		struct BJSONDecoder
		{
			Reader& reader;
			std::vector<std::string> strings;
			u32 valueCount = 0;

			bool String(const std::string*& outStr)
			{
				u64 index;
				if (!reader.VarU64(index) || index >= strings.size())
				{
					return reader.Fail();
				}
				outStr = &strings[(size_t)index];
				return true;
			}

			static JSONValue Integer(bool bNegative, u64 magnitude)
			{
				// As JSONValue::TypeFromChar & JSONParser pick integer types
				if (bNegative)
				{
					const i64 value = -(i64)(magnitude & 0x7FFFFFFFFFFFFFFFull) - 1;
					if (value < -(i64)INT_MAX)
					{
						return JSONValue(value);
					}
					return JSONValue((i32)value);
				}
				if (magnitude > (u64)INT_MAX)
				{
					return JSONValue(magnitude);
				}
				return JSONValue((u32)magnitude);
			}

			bool Floats(real* out, u32 count)
			{
				for (u32 i = 0; i < count; ++i)
				{
					if (!reader.Read(out[i]))
					{
						return false;
					}
				}
				return true;
			}

			bool Value(JSONValue& outValue, u32 depth)
			{
				u8 tag;
				if (!reader.Read(tag) || ++valueCount > BJSON_MAX_VALUES)
				{
					return reader.Fail();
				}
				switch (tag)
				{
				case BJ_STRING:
				{
					const std::string* str;
					if (!String(str))
					{
						return false;
					}
					// As JSONParser reads it (strings holding vectors become vectors)
					outValue = JSONParser::ValueFromString(*str);
				} return true;
				case BJ_GUID_STRING:
				{
					GUID guid;
					if (!reader.Read(guid.Data1) || !reader.Read(guid.Data2))
					{
						return false;
					}
					outValue = JSONValue(guid.ToString());
				} return true;
				case BJ_POS_INT:
				case BJ_NEG_INT:
				{
					u64 magnitude;
					if (!reader.VarU64(magnitude))
					{
						return false;
					}
					outValue = Integer(tag == BJ_NEG_INT, magnitude);
				} return true;
				case BJ_FLOAT:
				{
					real f;
					if (!reader.Read(f))
					{
						return false;
					}
					outValue = JSONValue(std::isfinite(f) ? f : 0.0f);
				} return true;
				case BJ_FALSE:
				case BJ_TRUE:
					outValue = JSONValue(tag == BJ_TRUE);
					return true;
				case BJ_VEC2:
				{
					glm::vec2 v;
					if (!Floats(&v.x, 2))
					{
						return false;
					}
					outValue = JSONValue(v);
				} return true;
				case BJ_VEC3:
				{
					glm::vec3 v;
					if (!Floats(&v.x, 3))
					{
						return false;
					}
					outValue = JSONValue(v);
				} return true;
				case BJ_VEC4:
				{
					glm::vec4 v;
					if (!Floats(&v.x, 4))
					{
						return false;
					}
					outValue = JSONValue(v);
				} return true;
				case BJ_OBJECT:
				{
					JSONObject object;
					if (!Object(object, depth + 1))
					{
						return false;
					}
					outValue = JSONValue(object);
				} return true;
				case BJ_OBJECT_ARRAY:
				{
					u32 count;
					if (depth + 1 > BJSON_MAX_DEPTH || !reader.Count(count, 1))
					{
						return reader.Fail();
					}
					std::vector<JSONObject> objects;
					objects.reserve(glm::min(count, 4096u));
					for (u32 i = 0; i < count; ++i)
					{
						objects.emplace_back();
						if (!Object(objects.back(), depth + 1))
						{
							return false;
						}
					}
					outValue = JSONValue(objects);
				} return true;
				case BJ_FIELD_ARRAY:
				{
					u32 count;
					if (depth + 1 > BJSON_MAX_DEPTH || !reader.Count(count, 1))
					{
						return reader.Fail();
					}
					std::vector<JSONField> fields;
					fields.reserve(glm::min(count, 4096u));
					for (u32 i = 0; i < count; ++i)
					{
						fields.emplace_back();
						if (!Value(fields.back().value, depth + 1))
						{
							return false;
						}
					}
					UnifyIntegerTypes(fields);
					outValue = JSONValue(fields);
				} return true;
				default:
					return reader.Fail();
				}
			}

			// JSONParser gives every integer in an array the same type
			static void UnifyIntegerTypes(std::vector<JSONField>& fields)
			{
				bool bRequireSigned = false;
				bool bRequireLong = false;
				for (const JSONField& field : fields)
				{
					const ValueType type = field.value.type;
					if (type != ValueType::INT && type != ValueType::UINT && type != ValueType::LONG && type != ValueType::ULONG)
					{
						return;
					}
					bRequireLong = bRequireLong || type == ValueType::LONG || type == ValueType::ULONG;
					bRequireSigned = bRequireSigned || type == ValueType::INT || type == ValueType::LONG;
				}
				for (JSONField& field : fields)
				{
					JSONValue& value = field.value;
					const i64 asSigned = value.type == ValueType::INT ? (i64)value.intValue :
						value.type == ValueType::UINT ? (i64)value.uintValue : value.longValue;
					if (bRequireLong)
					{
						value = bRequireSigned ? JSONValue(asSigned) : JSONValue((u64)asSigned);
					}
					else
					{
						value = bRequireSigned ? JSONValue((i32)asSigned) : JSONValue((u32)asSigned);
					}
				}
			}

			bool Object(JSONObject& outObject, u32 depth)
			{
				u32 count;
				if (depth > BJSON_MAX_DEPTH || !reader.Count(count, 2))
				{
					return reader.Fail();
				}
				outObject.fields.reserve(glm::min(count, 4096u));
				for (u32 i = 0; i < count; ++i)
				{
					outObject.fields.emplace_back();
					JSONField& field = outObject.fields.back();
					const std::string* label;
					if (!String(label) || !Value(field.value, depth))
					{
						return false;
					}
					field.label = *label;
				}
				return true;
			}
		};

		bool DecodeJSON(const u8* data, size_t size, JSONObject& outObject)
		{
			outObject = {};
			if (size > BJSON_MAX_SIZE)
			{
				return false;
			}
			Reader reader(data, size);
			u8 version;
			u32 stringCount;
			if (!reader.Read(version) || version != BJSON_VERSION || !reader.Count(stringCount, 1))
			{
				return false;
			}
			BJSONDecoder decoder{ reader, {}, 0 };
			decoder.strings.resize(stringCount);
			for (std::string& str : decoder.strings)
			{
				if (!reader.String(str))
				{
					return false;
				}
			}
			if (!decoder.Object(outObject, 0) || !reader.AtEnd())
			{
				outObject = {};
				return false;
			}
			return true;
		}

		static bool FloatsEquivalent(real a, real b)
		{
			if (a == b || (std::isnan(a) && std::isnan(b)))
			{
				return true;
			}
			// Text saves round to some number of decimal places
			const real tolerance = 1e-5f * glm::max(1.0f, std::abs(a));
			real scale = 1.0f;
			for (u32 precision = 0; precision <= 9; ++precision, scale *= 10.0f)
			{
				const real rounded = (real)(std::nearbyint((double)b * scale) / scale);
				if (std::abs(rounded - a) <= tolerance || std::abs((real)(std::nearbyint((double)a * scale) / scale) - b) <= tolerance)
				{
					return true;
				}
			}
			return std::abs(a - b) <= tolerance;
		}

		static bool ValuesEquivalent(const JSONValue& a, const JSONValue& b, const std::string& path, std::string* outDifference);

		static bool ObjectsEquivalent(const JSONObject& a, const JSONObject& b, const std::string& path, std::string* outDifference)
		{
			if (a.fields.size() != b.fields.size())
			{
				if (outDifference != nullptr)
				{
					*outDifference = path + ": field count " + std::to_string(a.fields.size()) + " vs " + std::to_string(b.fields.size());
				}
				return false;
			}
			for (size_t i = 0; i < a.fields.size(); ++i)
			{
				const std::string fieldPath = path + "/" + a.fields[i].label;
				if (a.fields[i].label != b.fields[i].label)
				{
					if (outDifference != nullptr)
					{
						*outDifference = fieldPath + ": label vs \"" + b.fields[i].label + "\"";
					}
					return false;
				}
				if (!ValuesEquivalent(a.fields[i].value, b.fields[i].value, fieldPath, outDifference))
				{
					return false;
				}
			}
			return true;
		}

		static bool ValuesEquivalent(const JSONValue& a, const JSONValue& b, const std::string& path, std::string* outDifference)
		{
			auto Differ = [&](const std::string& what)
			{
				if (outDifference != nullptr)
				{
					*outDifference = path + ": " + what;
				}
				return false;
			};

			if (a.type != b.type)
			{
				return Differ("type " + std::to_string((i32)a.type) + " vs " + std::to_string((i32)b.type));
			}
			switch (a.type)
			{
			case ValueType::STRING: return a.strValue == b.strValue || Differ("\"" + a.strValue + "\" vs \"" + b.strValue + "\"");
			case ValueType::INT:
			case ValueType::UINT:
			case ValueType::BOOL:
				return a.uintValue == b.uintValue || Differ(std::to_string(a.uintValue) + " vs " + std::to_string(b.uintValue));
			case ValueType::LONG:
			case ValueType::ULONG:
				return a.ulongValue == b.ulongValue || Differ(std::to_string(a.ulongValue) + " vs " + std::to_string(b.ulongValue));
			case ValueType::FLOAT: return FloatsEquivalent(a.floatValue, b.floatValue) || Differ(std::to_string(a.floatValue) + " vs " + std::to_string(b.floatValue));
			case ValueType::VEC2:
			case ValueType::VEC3:
			case ValueType::VEC4:
			case ValueType::QUAT:
			{
				const u32 count = a.type == ValueType::VEC2 ? 2 : a.type == ValueType::VEC3 ? 3 : 4;
				for (u32 i = 0; i < count; ++i)
				{
					if (!FloatsEquivalent(a.vecValue[i], b.vecValue[i]))
					{
						return Differ("component " + std::to_string(i) + " " + std::to_string(a.vecValue[i]) + " vs " + std::to_string(b.vecValue[i]));
					}
				}
			} return true;
			case ValueType::GUID: return a.guidValue == b.guidValue || Differ("guid");
			case ValueType::OBJECT: return ObjectsEquivalent(a.objectValue, b.objectValue, path, outDifference);
			case ValueType::OBJECT_ARRAY:
				if (a.objectArrayValue.size() != b.objectArrayValue.size())
				{
					return Differ("array size");
				}
				for (size_t i = 0; i < a.objectArrayValue.size(); ++i)
				{
					if (!ObjectsEquivalent(a.objectArrayValue[i], b.objectArrayValue[i], path + "[" + std::to_string(i) + "]", outDifference))
					{
						return false;
					}
				}
				return true;
			case ValueType::FIELD_ARRAY:
				if (a.fieldArrayValue.size() != b.fieldArrayValue.size())
				{
					return Differ("array size");
				}
				for (size_t i = 0; i < a.fieldArrayValue.size(); ++i)
				{
					if (!ValuesEquivalent(a.fieldArrayValue[i].value, b.fieldArrayValue[i].value, path + "[" + std::to_string(i) + "]", outDifference))
					{
						return false;
					}
				}
				return true;
			default:
				return true;
			}
		}

		bool JSONEquivalent(const JSONObject& a, const JSONObject& b, std::string* outDifference)
		{
			return ObjectsEquivalent(a, b, "", outDifference);
		}

		// -----------------------------------------------------------------------------------------
		// Worlds
		// -----------------------------------------------------------------------------------------

		static const u32 WORLD_SECTION_META = FourCC('M', 'E', 'T', 'A');

		// Bulk root fields get their own (compressed) sections, so listing worlds only reads the small ones
		struct WorldBulkField
		{
			const char* label;
			u32 tag;
		};
		static const WorldBulkField WORLD_BULK_FIELDS[] = {
			{ "player", FourCC('P', 'L', 'Y', 'R') },
			{ "inventory", FourCC('I', 'N', 'V', 'T') },
			{ "objects", FourCC('O', 'B', 'J', 'S') },
			{ "opened chests", FourCC('C', 'H', 'S', 'T') },
			{ "explored", FourCC('E', 'X', 'P', 'L') }, // (Explored area, for the world's stats)
		};

		bool WorldFileExists(const std::string& directory)
		{
			WaitForPendingWrites();
			return FileExists(directory + WORLD_FILE_NAME) || FileExists(directory + WORLD_FILE_NAME + ".bak") || FileExists(directory + LEGACY_WORLD_FILE_NAME);
		}

		// The last world file write (when asynchronous)
		static std::mutex s_PendingWriteMutex;
		static std::future<bool> s_PendingWrite;

		bool WriteWorldFile(const std::string& directory, const JSONObject& worldObj, WorldFileStats* outStats, bool bAsync)
		{
			const u64 start = Ticks();

			JSONObject meta;
			JSONObject bulk[ARRAY_LENGTH(WORLD_BULK_FIELDS)];
			for (const JSONField& field : worldObj.fields)
			{
				bool bBulk = false;
				for (u32 i = 0; i < ARRAY_LENGTH(WORLD_BULK_FIELDS); ++i)
				{
					if (field.label == WORLD_BULK_FIELDS[i].label)
					{
						bulk[i].fields.push_back(field);
						bBulk = true;
						break;
					}
				}
				if (!bBulk)
				{
					meta.fields.push_back(field);
				}
			}

			FileWriter writer(WORLD_FILE_TYPE, WORLD_FILE_VERSION);
			// (Uncompressed: it's small & read whenever worlds are listed)
			writer.AddJSONSection(WORLD_SECTION_META, meta, false);
			for (u32 i = 0; i < ARRAY_LENGTH(WORLD_BULK_FIELDS); ++i)
			{
				if (!bulk[i].fields.empty())
				{
					writer.AddJSONSection(WORLD_BULK_FIELDS[i].tag, bulk[i], true);
				}
			}
			std::vector<u8> bytes = writer.Build();
			const u64 encoded = Ticks();

			if (outStats != nullptr)
			{
				outStats->sizeBytes = bytes.size();
				outStats->encodeMs = (ms)((double)(encoded - start) / 1e6);
			}

			auto Write = [directory](const std::vector<u8>& fileBytes)
			{
				const std::string filePath = directory + WORLD_FILE_NAME;
				if (!WriteFileAtomic(filePath, fileBytes.data(), fileBytes.size(), true))
				{
					PrintError("Failed to write %s\n", filePath.c_str());
					return false;
				}

				// Migrated: the text version is kept as a backup, but no longer read
				const std::string legacyPath = directory + LEGACY_WORLD_FILE_NAME;
				if (FileExists(legacyPath))
				{
					const std::string legacyBackupPath = legacyPath + ".bak";
					Platform::DeleteFile(legacyBackupPath, false);
					if (std::rename(legacyPath.c_str(), legacyBackupPath.c_str()) != 0)
					{
						PrintWarn("Failed to rename old world file %s\n", legacyPath.c_str());
					}
				}
				return true;
			};

			// One write at a time, in order
			WaitForPendingWrites();
			bool bSaved = true;
			if (bAsync)
			{
				std::lock_guard<std::mutex> lock(s_PendingWriteMutex);
				s_PendingWrite = std::async(std::launch::async, [Write, fileBytes = std::move(bytes)]() { return Write(fileBytes); });
			}
			else
			{
				bSaved = Write(bytes);
			}

			if (outStats != nullptr)
			{
				outStats->ioMs = MsSince(encoded);
			}
			return bSaved;
		}

		void WaitForPendingWrites()
		{
			std::lock_guard<std::mutex> lock(s_PendingWriteMutex);
			if (s_PendingWrite.valid())
			{
				s_PendingWrite.get();
			}
		}

		static bool ReadBinaryWorld(const std::string& filePath, JSONObject& outWorldObj, bool bMetadataOnly, LoadResult& outResult)
		{
			static const std::vector<u32> metadataTags = { WORLD_SECTION_META };
			FileReader reader;
			outResult = reader.Load(filePath, WORLD_FILE_TYPE, bMetadataOnly ? &metadataTags : nullptr);
			if (outResult != LoadResult::OK)
			{
				return false;
			}
			if (reader.FileVersion() > WORLD_FILE_VERSION)
			{
				PrintWarn("%s is from a newer version (%u), reading what's understood\n", filePath.c_str(), reader.FileVersion());
			}
			outWorldObj = {};
			if (!reader.HasSection(WORLD_SECTION_META) || !reader.GetJSONFields(outWorldObj))
			{
				outResult = LoadResult::CORRUPT;
				outWorldObj = {};
				return false;
			}
			return true;
		}

		WorldFileSource ReadWorldFile(const std::string& directory, JSONObject& outWorldObj, bool bMetadataOnly, WorldFileStats* outStats)
		{
			WaitForPendingWrites();
			const u64 start = Ticks();
			WorldFileSource source = WorldFileSource::NONE;
			outWorldObj = {};

			const std::string filePath = directory + WORLD_FILE_NAME;
			LoadResult result;
			if (ReadBinaryWorld(filePath, outWorldObj, bMetadataOnly, result))
			{
				source = WorldFileSource::BINARY;
			}
			else
			{
				if (result != LoadResult::MISSING)
				{
					PrintError("World file %s is %s, trying its backup\n", filePath.c_str(), LoadResultToString(result));
				}
				LoadResult backupResult;
				if (ReadBinaryWorld(filePath + ".bak", outWorldObj, bMetadataOnly, backupResult))
				{
					source = WorldFileSource::BINARY_BACKUP;
					if (result == LoadResult::MISSING)
					{
						PrintWarn("World file %s is missing, using its backup\n", filePath.c_str());
					}
				}
				else
				{
					const std::string legacyPath = directory + LEGACY_WORLD_FILE_NAME;
					if (FileExists(legacyPath) && JSONParser::ParseFromFile(legacyPath, outWorldObj))
					{
						source = WorldFileSource::LEGACY_JSON;
					}
					else
					{
						outWorldObj = {};
					}
				}
			}

			if (outStats != nullptr)
			{
				const std::string path = source == WorldFileSource::LEGACY_JSON ? directory + LEGACY_WORLD_FILE_NAME :
					source == WorldFileSource::BINARY_BACKUP ? filePath + ".bak" : filePath;
				std::ifstream file(path, std::ios::binary | std::ios::ate);
				outStats->sizeBytes = file ? (size_t)file.tellg() : 0;
				outStats->ioMs = MsSince(start);
			}
			return source;
		}

		const char* WorldFileSourceToString(WorldFileSource source)
		{
			switch (source)
			{
			case WorldFileSource::BINARY: return "binary";
			case WorldFileSource::BINARY_BACKUP: return "binary backup";
			case WorldFileSource::LEGACY_JSON: return "json (legacy)";
			default: return "none";
			}
		}
	} // namespace save
} // namespace flex

// -----------------------------------------------------------------------------------------
// Tests (run with --test)
// -----------------------------------------------------------------------------------------

namespace flex
{
	namespace save
	{
		namespace
		{
			struct TestRandom
			{
				u64 state;
				u64 Next()
				{
					state ^= state << 13;
					state ^= state >> 7;
					state ^= state << 17;
					return state;
				}
				u32 Range(u32 count) { return count == 0 ? 0 : (u32)(Next() % count); }
				real Float(real lo, real hi) { return lo + (hi - lo) * (real)(Next() % 1000000) / 1000000.0f; }
			};

			struct TestContext
			{
				u32 failures = 0;
				void Check(bool bCondition, const char* what, i32 line)
				{
					if (!bCondition)
					{
						++failures;
						PrintError("SaveFile test failed (L%d): %s\n", line, what);
					}
				}
			};
#define SAVE_CHECK(cond) ctx.Check((cond), #cond, __LINE__)

			// Damaged files are expected to log errors while fuzzing
			struct QuietLog
			{
				bool bWasEnabled = g_bEnableLogToConsole;
				QuietLog() { g_bEnableLogToConsole = false; }
				~QuietLog() { g_bEnableLogToConsole = bWasEnabled; }
			};

			// Random damage: bit flips, byte overwrites, truncation, insertion, deletion & overwritten sizes
			static void Mutate(std::vector<u8>& bytes, TestRandom& random)
			{
				const u32 mutationCount = 1 + random.Range(4);
				for (u32 m = 0; m < mutationCount; ++m)
				{
					const u32 kind = random.Range(6);
					const size_t size = bytes.size();
					if (size == 0)
					{
						bytes.push_back((u8)random.Next());
						continue;
					}
					const size_t pos = random.Range((u32)size);
					switch (kind)
					{
					case 0: bytes[pos] ^= (u8)(1u << random.Range(8)); break;
					case 1: bytes[pos] = (u8)random.Next(); break;
					case 2: bytes.resize(pos); break;
					case 3: bytes.insert(bytes.begin() + pos, (u8)random.Next()); break;
					case 4: bytes.erase(bytes.begin() + pos); break;
					case 5:
					{
						// A plausible looking size or count
						const u32 value = random.Range(2) ? (u32)random.Next() : random.Range(64);
						for (u32 i = 0; i < 4 && pos + i < size; ++i)
						{
							bytes[pos + i] = (u8)(value >> (i * 8));
						}
					} break;
					}
				}
			}

			static JSONObject MakeTestTree()
			{
				JSONObject child;
				child.fields.emplace_back("pos", JSONValue(glm::vec3(1.5f, -2.25f, 3.125f), 3));
				child.fields.emplace_back("rot", JSONValue(glm::quat(0.9f, 0.1f, 0.2f, 0.3f), 4));
				child.fields.emplace_back("scale", JSONValue(glm::vec2(2.0f, 0.5f)));
				child.fields.emplace_back("colour", JSONValue(glm::vec4(0.1f, 0.2f, 0.3f, 1.0f)));
				child.fields.emplace_back("vec string", JSONValue("1.0, 2.0, 3.0"));

				JSONObject root;
				root.fields.emplace_back("version", JSONValue(7));
				root.fields.emplace_back("name", JSONValue("A world, with commas"));
				root.fields.emplace_back("empty string", JSONValue(""));
				root.fields.emplace_back("negative", JSONValue(-12345));
				root.fields.emplace_back("int min", JSONValue((i32)INT_MIN));
				root.fields.emplace_back("uint", JSONValue(4000000000u));
				root.fields.emplace_back("long", JSONValue((i64)-9000000000ll));
				root.fields.emplace_back("ulong", JSONValue((u64)18000000000000000000ull));
				root.fields.emplace_back("float", JSONValue(3.14159f));
				root.fields.emplace_back("float 1dp", JSONValue(1399.2f, 1));
				root.fields.emplace_back("float 0dp", JSONValue(7.6f, 0));
				root.fields.emplace_back("bool t", JSONValue(true));
				root.fields.emplace_back("bool f", JSONValue(false));
				root.fields.emplace_back("guid", JSONValue(GUID(0x0123456789ABCDEFull, 0xFEDCBA9876543210ull)));
				root.fields.emplace_back("guid string", JSONValue("07F5D8C7F068B2804EAB0770F53DF477"));
				root.fields.emplace_back("not quite a guid", JSONValue("07f5d8c7f068b2804eab0770f53df477"));
				root.fields.emplace_back("matrix", JSONValue(glm::mat4(1.0f), 2));
				root.fields.emplace_back("child", JSONValue(child));
				root.fields.emplace_back("empty object", JSONValue(JSONObject()));
				root.fields.emplace_back("objects", JSONValue(std::vector<JSONObject>{ child, child, JSONObject() }));
				root.fields.emplace_back("no objects", JSONValue(std::vector<JSONObject>{}));
				std::vector<JSONField> ints = { JSONField("", JSONValue(5)), JSONField("", JSONValue(-3)), JSONField("", JSONValue(7u)) };
				root.fields.emplace_back("ints", JSONValue(ints));
				std::vector<JSONField> strings = { JSONField("", JSONValue("123")), JSONField("", JSONValue("abc")) };
				root.fields.emplace_back("strings", JSONValue(strings));
				return root;
			}

			// The binary JSON path must read back what the text path does
			static bool CheckMatchesText(const JSONObject& tree, std::string& outDifference)
			{
				std::vector<u8> bytes;
				EncodeJSON(tree, bytes);
				JSONObject fromBinary;
				if (!DecodeJSON(bytes.data(), bytes.size(), fromBinary))
				{
					outDifference = "decode failed";
					return false;
				}
				JSONObject fromText;
				if (!JSONParser::Parse(tree.ToString(), fromText))
				{
					// Text can't represent it, so there's nothing to match
					return true;
				}
				return JSONEquivalent(fromText, fromBinary, &outDifference);
			}

			static void TestPrimitives(TestContext& ctx)
			{
				Writer writer;
				const u64 values[] = { 0, 1, 127, 128, 300, 16383, 16384, 0xFFFFFFFFull, 0x123456789ABCull, ~0ull };
				for (u64 value : values)
				{
					writer.VarU64(value);
				}
				writer.String("hello");
				Reader reader(writer.bytes.data(), writer.bytes.size());
				for (u64 value : values)
				{
					u64 read = 0;
					SAVE_CHECK(reader.VarU64(read) && read == value);
				}
				std::string str;
				SAVE_CHECK(reader.String(str) && str == "hello");
				SAVE_CHECK(reader.AtEnd());
				u8 byte;
				SAVE_CHECK(!reader.Read(byte) && reader.Failed());

				// Overlong varint & counts larger than the data
				const u8 overlong[11] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
				Reader overlongReader(overlong, sizeof(overlong));
				u64 v;
				SAVE_CHECK(!overlongReader.VarU64(v));
				const u8 bigCount[3] = { 0x80, 0x01, 0x00 }; // 128 elements in 1 byte
				Reader countReader(bigCount, sizeof(bigCount));
				u32 count;
				SAVE_CHECK(!countReader.Count(count, 1));
				const u8 bigString[2] = { 0x05, 'a' };
				Reader stringReader(bigString, sizeof(bigString));
				SAVE_CHECK(!stringReader.String(str));
				Reader nullReader(nullptr, 100);
				SAVE_CHECK(!nullReader.Read(byte));
			}

			static void TestCompression(TestContext& ctx, TestRandom& random)
			{
				// Matches zlib's crc32
				const char* crcInput = "123456789";
				SAVE_CHECK(Crc32(crcInput, 9) == 0xCBF43926u);

				// Streams from zlib itself: dynamic Huffman & stored blocks
				const u8 dynamicStream[] = { 0x78,0xDA,0x95,0xD2,0x3B,0x0A,0xC3,0x30,0x10,0x84,0xE1,0xAB,0xE8,0x08,0xD1,0x5B,0x22,0xA7,0x09,0xC4,0x06,0x83,0x49,0x61,0x5C,0xE4,0xF8,0x21,0x75,0xBE,0x22,0x6E,0x87,0xD5,0x68,0xF6,0x9F,0x5D,0xF7,0xE5,0x1D,0x6E,0xE1,0x5C,0x8E,0xE3,0xB1,0xBD,0xC2,0xF2,0xDC,0xCE,0x7B,0x58,0xBF,0x62,0x94,0x58,0x24,0x4E,0x3E,0x6F,0x52,0x53,0x95,0x9A,0x39,0x5B,0xE8,0xDB,0x18,0x61,0x30,0x6D,0x66,0x04,0x1A,0x94,0x2E,0xB5,0x27,0x3A,0xF0,0x2F,0x26,0x68,0x9C,0x9D,0xA6,0xC0,0xB8,0xDD,0xE5,0xD0,0xB7,0xD2,0x61,0x9A,0xAE,0xAB,0x74,0xEB,0xF6,0xA5,0x43,0x65,0xDE,0x41,0x38,0xC4,0x90,0xD8,0xC4,0xA0,0x43,0x65,0x84,0x44,0x38,0xC3,0xE7,0xE4,0xC3,0x61,0xB2,0xC8,0xD9,0x41,0xBC,0xCD,0xD0,0xB9,0x45,0x66,0xDE,0x68,0x66,0x6C,0x82,0xFD,0x0C,0x2E,0xD1,0x89,0xA1,0x7B,0x36,0x5F,0x50,0x2F,0xFC,0xE6,0x64,0xDE,0xA2,0xFD,0xCF,0xC6,0x1C,0xCD,0xDC,0xFD,0xFC,0x74,0xF9,0x01,0x72,0x49,0xBC,0x29 };
				std::string expected;
				for (i32 i = 0; i < 60; ++i)
				{
					expected += "flex " + std::to_string(i * i % 97) + " terrain edit; ";
				}
				std::vector<u8> raw;
				SAVE_CHECK(Decompress(dynamicStream, sizeof(dynamicStream), expected.size(), raw) && memcmp(raw.data(), expected.data(), expected.size()) == 0);
				SAVE_CHECK(Crc32(expected.data(), expected.size()) == 2517933543u);
				SAVE_CHECK(!Decompress(dynamicStream, sizeof(dynamicStream), expected.size() - 1, raw));
				SAVE_CHECK(!Decompress(dynamicStream, sizeof(dynamicStream), expected.size() + 1, raw));
				const u8 storedStream[] = { 0x78,0x01,0x01,0x11,0x00,0xEE,0xFF,0x73,0x74,0x6F,0x72,0x65,0x64,0x20,0x62,0x6C,0x6F,0x63,0x6B,0x20,0x74,0x65,0x73,0x74,0x3C,0x2A,0x06,0x9D };
				SAVE_CHECK(Decompress(storedStream, sizeof(storedStream), 17, raw) && memcmp(raw.data(), "stored block test", 17) == 0);

				// Round trips through the writer's compressor
				for (u32 test = 0; test < 24; ++test)
				{
					const u32 size = test < 4 ? 64 + test : 64 + random.Range(200000);
					std::vector<u8> data(size);
					for (u32 i = 0; i < size; ++i)
					{
						switch (test % 4)
						{
						case 0: data[i] = 0; break;
						case 1: data[i] = (u8)random.Next(); break;
						case 2: data[i] = (u8)("terrain edits & world objects "[i % 30]); break;
						case 3: data[i] = (u8)(random.Range(4) == 0 ? random.Next() : i / 64); break;
						}
					}
					std::vector<u8> compressed;
					SAVE_CHECK(Compress(data.data(), data.size(), compressed));
					SAVE_CHECK(Decompress(compressed.data(), compressed.size(), data.size(), raw) && raw == data);
				}

				// Damaged streams must fail (or decode to *something* of the right size) without reading or writing out of bounds
				std::vector<u8> source(expected.begin(), expected.end());
				std::vector<u8> compressed;
				Compress(source.data(), source.size(), compressed);
				std::vector<std::vector<u8>> seeds = { compressed, std::vector<u8>(dynamicStream, dynamicStream + sizeof(dynamicStream)),
					std::vector<u8>(storedStream, storedStream + sizeof(storedStream)) };
				for (u32 i = 0; i < 30000; ++i)
				{
					std::vector<u8> damaged = seeds[i % seeds.size()];
					Mutate(damaged, random);
					const size_t rawSize = random.Range(2) ? source.size() : random.Range(4096);
					if (Decompress(damaged.data(), damaged.size(), rawSize, raw))
					{
						SAVE_CHECK(raw.size() == rawSize);
					}
				}
			}

			static void TestContainer(TestContext& ctx, TestRandom& random)
			{
				const u32 TEST_TYPE = FourCC('T', 'E', 'S', 'T');
				std::vector<u8> compressible(5000);
				for (size_t i = 0; i < compressible.size(); ++i)
				{
					compressible[i] = (u8)(i % 7);
				}
				std::vector<u8> small = { 1, 2, 3 };

				FileWriter writer(TEST_TYPE, 3);
				writer.AddJSONSection(FourCC('J', 'S', 'O', 'N'), MakeTestTree(), true);
				writer.AddSection(FourCC('B', 'I', 'G', ' '), compressible, 0, true);
				writer.AddSection(FourCC('S', 'M', 'A', 'L'), small, 0, true);
				writer.AddSection(FourCC('E', 'M', 'P', 'T'), {}, 0, true);
				writer.AddSection(FourCC('N', 'E', 'W', ' '), small, 1u << 8, false);  // A required flag from the future
				writer.AddSection(FourCC('O', 'P', 'T', ' '), small, 1u << 20, false); // An optional one
				const std::vector<u8> bytes = writer.Build();

				FileReader reader;
				SAVE_CHECK(reader.Parse(bytes.data(), bytes.size(), TEST_TYPE) == LoadResult::OK);
				SAVE_CHECK(reader.FileVersion() == 3);
				SAVE_CHECK((reader.SectionFlags(FourCC('B', 'I', 'G', ' ')) & SECTION_ZLIB) != 0);
				std::vector<u8> raw;
				SAVE_CHECK(reader.GetSection(FourCC('B', 'I', 'G', ' '), raw) && raw == compressible);
				SAVE_CHECK(reader.GetSection(FourCC('S', 'M', 'A', 'L'), raw) && raw == small);
				SAVE_CHECK(reader.GetSection(FourCC('E', 'M', 'P', 'T'), raw) && raw.empty());
				SAVE_CHECK(!reader.GetSection(FourCC('N', 'E', 'W', ' '), raw));
				SAVE_CHECK(reader.GetSection(FourCC('O', 'P', 'T', ' '), raw) && raw == small);
				SAVE_CHECK(!reader.GetSection(FourCC('M', 'I', 'S', 'S'), raw));
				JSONObject fields;
				SAVE_CHECK(reader.GetJSONFields(fields) && fields.HasField("child"));
				SAVE_CHECK(reader.Parse(bytes.data(), bytes.size(), FourCC('O', 'T', 'H', 'R')) == LoadResult::WRONG_TYPE);
				const char* text = "{ \"json\" : true }";
				SAVE_CHECK(reader.Parse((const u8*)text, strlen(text), TEST_TYPE) == LoadResult::NOT_CONTAINER);
				SAVE_CHECK(reader.Parse(nullptr, 0, TEST_TYPE) == LoadResult::CORRUPT);

				// Every truncation is detected
				for (size_t size = 0; size < bytes.size(); ++size)
				{
					SAVE_CHECK(reader.Parse(bytes.data(), size, TEST_TYPE) != LoadResult::OK);
				}

				// Any single damaged byte is either detected, or doesn't change what's read
				const std::vector<u32> tags = { FourCC('J', 'S', 'O', 'N'), FourCC('B', 'I', 'G', ' '), FourCC('S', 'M', 'A', 'L'), FourCC('O', 'P', 'T', ' ') };
				FileReader original;
				original.Parse(bytes.data(), bytes.size(), TEST_TYPE);
				std::vector<std::vector<u8>> originalSections;
				for (u32 tag : tags)
				{
					originalSections.emplace_back();
					original.GetSection(tag, originalSections.back());
				}
				for (size_t pos = 0; pos < bytes.size(); ++pos)
				{
					std::vector<u8> damaged = bytes;
					damaged[pos] ^= (u8)(1u << (pos % 8));
					if (reader.Parse(damaged.data(), damaged.size(), TEST_TYPE) != LoadResult::OK)
					{
						continue;
					}
					for (size_t t = 0; t < tags.size(); ++t)
					{
						if (reader.GetSection(tags[t], raw))
						{
							SAVE_CHECK(raw == originalSections[t]);
						}
					}
				}

				// Random damage never crashes
				for (u32 i = 0; i < 20000; ++i)
				{
					std::vector<u8> damaged = bytes;
					Mutate(damaged, random);
					if (reader.Parse(damaged.data(), damaged.size(), TEST_TYPE) == LoadResult::OK)
					{
						for (u32 tag : reader.Tags())
						{
							reader.GetSection(tag, raw);
						}
						JSONObject damagedFields;
						reader.GetJSONFields(damagedFields);
					}
				}
			}

			static void TestBinaryJSON(TestContext& ctx, TestRandom& random)
			{
				const JSONObject tree = MakeTestTree();
				std::string difference;
				SAVE_CHECK(CheckMatchesText(tree, difference));
				if (!difference.empty())
				{
					PrintError("  %s\n", difference.c_str());
				}

				// Exact values (text rounds floats, binary doesn't)
				std::vector<u8> bytes;
				EncodeJSON(tree, bytes);
				JSONObject decoded;
				SAVE_CHECK(DecodeJSON(bytes.data(), bytes.size(), decoded));
				SAVE_CHECK(decoded.GetFloat("float") == 3.14159f);
				SAVE_CHECK(decoded.GetString("name") == "A world, with commas");
				SAVE_CHECK(decoded.GetString("guid string") == "07F5D8C7F068B2804EAB0770F53DF477");

				// The decoder is fed damaged data directly (as if a section's checksum had matched by chance)
				for (u32 i = 0; i < 30000; ++i)
				{
					std::vector<u8> damaged = bytes;
					Mutate(damaged, random);
					JSONObject result;
					DecodeJSON(damaged.data(), damaged.size(), result);
				}

				// Deep nesting is rejected rather than overflowing the stack
				std::vector<u8> deep = { 1, 1, 0 }; // Version, one string ("")
				for (u32 i = 0; i < 100000; ++i)
				{
					deep.push_back(1); // One field
					deep.push_back(0); // Label 0
					deep.push_back(BJ_OBJECT);
				}
				JSONObject result;
				SAVE_CHECK(!DecodeJSON(deep.data(), deep.size(), result));
			}

			static std::vector<std::string> FindJSONFiles()
			{
				std::vector<std::string> filePaths;
				const char* directories[] = { RESOURCE_DIRECTORY "scenes/", PREFAB_DIRECTORY, SAVED_DIRECTORY "worlds/" };
				for (const char* directory : directories)
				{
					// (There are no saved worlds on CI)
					if (Platform::DirectoryExists(RelativePathToAbsolute(directory)))
					{
						Platform::FindFilesInDirectory(directory, filePaths, "*", true);
					}
				}
				std::vector<std::string> jsonFiles;
				for (const std::string& filePath : filePaths)
				{
					if (EndsWith(filePath, ".json") || EndsWith(filePath, "world.json.bak"))
					{
						jsonFiles.push_back(filePath);
					}
				}
				return jsonFiles;
			}

			static void TestRealFiles(TestContext& ctx)
			{
				u32 fileCount = 0;
				size_t textSize = 0, binarySize = 0, compressedSize = 0;
				for (const std::string& filePath : FindJSONFiles())
				{
					JSONObject tree;
					if (!JSONParser::ParseFromFile(filePath, tree))
					{
						continue;
					}
					++fileCount;
					std::string difference;
					const bool bMatches = CheckMatchesText(tree, difference);
					SAVE_CHECK(bMatches);
					if (!bMatches)
					{
						PrintError("  %s: %s\n", filePath.c_str(), difference.c_str());
					}
					std::vector<u8> bytes, compressed;
					EncodeJSON(tree, bytes);
					textSize += tree.ToString().size();
					binarySize += bytes.size();
					compressedSize += Compress(bytes.data(), bytes.size(), compressed) ? compressed.size() : bytes.size();
				}
				// (Fails if the resources aren't found, rather than silently testing nothing)
				SAVE_CHECK(fileCount > 0);
				Print("  Binary JSON matches text for %u files (text %u KB, binary %u KB, compressed %u KB)\n", fileCount,
					(u32)(textSize / 1024), (u32)(binarySize / 1024), (u32)(compressedSize / 1024));
			}

			static std::string TestDirectory()
			{
				const std::string directory = RelativePathToAbsolute(SAVED_DIRECTORY "save_file_test/");
				Platform::CreateDirectoryRecursive(directory);
				std::vector<std::string> filePaths;
				Platform::FindFilesInDirectory(directory, filePaths, "*", false);
				for (const std::string& filePath : filePaths)
				{
					Platform::DeleteFile(filePath, false);
				}
				return directory;
			}

			// Sections are merged back in their own order, so a world's root fields are compared by name
			static JSONObject SortedFields(JSONObject object)
			{
				std::stable_sort(object.fields.begin(), object.fields.end(), [](const JSONField& a, const JSONField& b) { return a.label < b.label; });
				return object;
			}

			static void TestWorldFiles(TestContext& ctx, TestRandom& random)
			{
				const std::string directory = TestDirectory();
				JSONObject world = MakeTestTree();
				world.fields.emplace_back("player", JSONValue(MakeTestTree()));
				world.fields.emplace_back("objects", JSONValue(std::vector<JSONObject>(20, MakeTestTree())));
				world.fields.emplace_back("torches placed", JSONValue(12)); // e.g. a stat added later

				// An old world only has world.json
				SAVE_CHECK(!WorldFileExists(directory));
				WriteFile(directory + LEGACY_WORLD_FILE_NAME, world.ToString(), false);
				SAVE_CHECK(WorldFileExists(directory));
				JSONObject fromText;
				SAVE_CHECK(ReadWorldFile(directory, fromText, false) == WorldFileSource::LEGACY_JSON);

				// Saving migrates it
				SAVE_CHECK(WriteWorldFile(directory, fromText));
				SAVE_CHECK(FileExists(directory + WORLD_FILE_NAME) && !FileExists(directory + LEGACY_WORLD_FILE_NAME) && FileExists(directory + LEGACY_WORLD_FILE_NAME + ".bak"));
				JSONObject fromBinary;
				SAVE_CHECK(ReadWorldFile(directory, fromBinary, false) == WorldFileSource::BINARY);
				std::string difference;
				SAVE_CHECK(JSONEquivalent(SortedFields(fromText), SortedFields(fromBinary), &difference));
				if (!difference.empty())
				{
					PrintError("  %s\n", difference.c_str());
				}

				// Listing only reads the small fields
				JSONObject metadata;
				SAVE_CHECK(ReadWorldFile(directory, metadata, true) == WorldFileSource::BINARY);
				SAVE_CHECK(metadata.HasField("name") && metadata.GetInt("torches placed") == 12 && !metadata.HasField("objects") && !metadata.HasField("player"));

				// A damaged file falls back to the previous save
				world.fields.emplace_back("second save", JSONValue(true));
				SAVE_CHECK(WriteWorldFile(directory, world));
				SAVE_CHECK(FileExists(directory + WORLD_FILE_NAME + ".bak"));
				std::vector<u8> bytes;
				ReadFileBytes(directory + WORLD_FILE_NAME, bytes);
				std::vector<u8> damaged = bytes;
				SAVE_CHECK(bytes.size() > 100);
				if (damaged.size() > 10)
				{
					damaged[damaged.size() - 10] ^= 0x10;
				}
				WriteFileAtomic(directory + WORLD_FILE_NAME, damaged.data(), damaged.size(), false);
				JSONObject recovered;
				SAVE_CHECK(ReadWorldFile(directory, recovered, false) == WorldFileSource::BINARY_BACKUP);
				SAVE_CHECK(!recovered.HasField("second save") && recovered.HasField("objects"));
				// ... as does a truncated or missing one
				WriteFileAtomic(directory + WORLD_FILE_NAME, bytes.data(), bytes.size() / 2, false);
				SAVE_CHECK(ReadWorldFile(directory, recovered, false) == WorldFileSource::BINARY_BACKUP);
				Platform::DeleteFile(directory + WORLD_FILE_NAME, false);
				SAVE_CHECK(ReadWorldFile(directory, recovered, true) == WorldFileSource::BINARY_BACKUP);

				// Random damage to the file never crashes the loader
				QuietLog quiet;
				Platform::DeleteFile(directory + WORLD_FILE_NAME + ".bak", false);
				Platform::DeleteFile(directory + LEGACY_WORLD_FILE_NAME + ".bak", false);
				for (u32 i = 0; i < 300; ++i)
				{
					damaged = bytes;
					Mutate(damaged, random);
					WriteFileAtomic(directory + WORLD_FILE_NAME, damaged.data(), damaged.size(), false);
					ReadWorldFile(directory, recovered, (i % 2) == 0);
				}

				// Asynchronous saves are finished before the world is read again
				world.fields.emplace_back("async save", JSONValue(true));
				SAVE_CHECK(WriteWorldFile(directory, world, nullptr, true));
				JSONObject asyncRead;
				SAVE_CHECK(ReadWorldFile(directory, asyncRead, true) == WorldFileSource::BINARY && asyncRead.HasField("async save"));
				TestDirectory();
			}

			static void WriteEditsV1(const std::string& filePath, i32 seed, const std::vector<terrain::Edit>& edits)
			{
				Writer writer;
				writer.Write((u32)0x44455446);
				writer.Write((u32)1);
				writer.Write(seed);
				writer.Write((u32)edits.size());
				for (const terrain::Edit& edit : edits)
				{
					writer.Write(edit.center);
					writer.Write(edit.radius);
					writer.Write((u8)edit.type);
					writer.Write((u8)edit.material);
				}
				WriteFileAtomic(filePath, writer.bytes.data(), writer.bytes.size(), false);
			}

			static std::vector<terrain::Edit> MakeEdits(TestRandom& random, u32 count)
			{
				// Clustered like real digging
				std::vector<terrain::Edit> edits(count);
				glm::vec3 pos(random.Float(-50000.0f, 50000.0f), random.Float(-500.0f, 1500.0f), random.Float(-50000.0f, 50000.0f));
				for (terrain::Edit& edit : edits)
				{
					if (random.Range(200) == 0)
					{
						pos = glm::vec3(random.Float(-50000.0f, 50000.0f), random.Float(-500.0f, 1500.0f), random.Float(-50000.0f, 50000.0f));
					}
					pos += glm::vec3(random.Float(-1.5f, 1.5f), random.Float(-1.5f, 1.0f), random.Float(-1.5f, 1.5f));
					edit.center = pos;
					edit.radius = random.Range(4) == 0 ? 2.5f : 1.6f;
					edit.type = (terrain::EditType)random.Range(2);
					edit.material = (terrain::Material)0;
				}
				return edits;
			}

			static bool EditsEqual(const std::vector<terrain::Edit>& a, const std::vector<terrain::Edit>& b)
			{
				if (a.size() != b.size())
				{
					return false;
				}
				for (size_t i = 0; i < a.size(); ++i)
				{
					if (memcmp(&a[i].center, &b[i].center, sizeof(glm::vec3)) != 0 || a[i].radius != b[i].radius || a[i].type != b[i].type || a[i].material != b[i].material)
					{
						return false;
					}
					if (terrain::IsPlaneEdit(a[i].type) && (memcmp(&a[i].normal, &b[i].normal, sizeof(glm::vec3)) != 0 || a[i].height != b[i].height))
					{
						return false;
					}
					if (terrain::IsBoxEdit(a[i].type) && (memcmp(&a[i].normal, &b[i].normal, sizeof(glm::vec3)) != 0 || a[i].height != b[i].height || a[i].twist != b[i].twist))
					{
						return false;
					}
				}
				return true;
			}

			static void TestTerrainEdits(TestContext& ctx, TestRandom& random)
			{
				const std::string directory = TestDirectory();
				const std::string filePath = directory + "terrain_42.edits";
				const std::vector<terrain::Edit> edits = MakeEdits(random, 3000);

				// Version 1 files are still read, & rewritten in the new format
				WriteEditsV1(filePath, 42, edits);
				std::vector<terrain::Edit> read;
				SAVE_CHECK(terrain::ReadEdits(filePath, 42, read) && EditsEqual(read, edits));
				SAVE_CHECK(!terrain::ReadEdits(filePath, 43, read));
				SAVE_CHECK(terrain::WriteEdits(filePath, 42, edits));
				SAVE_CHECK(terrain::ReadEdits(filePath, 42, read) && EditsEqual(read, edits));
				SAVE_CHECK(terrain::WriteEdits(filePath, 42, {}));
				SAVE_CHECK(terrain::ReadEdits(filePath, 42, read) && read.empty());

				// Plane edits (flatten/lower/raise brushes) keep their normal & height
				{
					std::vector<terrain::Edit> planeEdits = edits;
					for (size_t i = 0; i < planeEdits.size(); i += 2)
					{
						const terrain::EditType type = (terrain::EditType)((u32)terrain::EditType::LOWER + (u32)random.Range(3));
						const glm::vec3 normal(random.Float(-1.0f, 1.0f), random.Float(0.2f, 1.0f), random.Float(-1.0f, 1.0f));
						planeEdits[i] = terrain::MakePlaneEdit(type, planeEdits[i].center, normal, random.Float(0.5f, 4.0f),
							random.Range(2) == 0 ? 0.0f : random.Float(0.5f, 6.0f), (terrain::Material)random.Range((u32)terrain::Material::_COUNT));
					}
					SAVE_CHECK(terrain::WriteEdits(filePath, 42, planeEdits));
					SAVE_CHECK(terrain::ReadEdits(filePath, 42, read) && EditsEqual(read, planeEdits));
					// ... & box edits (mining) their normal, depth & twist
					for (size_t i = 1; i < planeEdits.size(); i += 4)
					{
						const glm::vec3 normal(random.Float(-1.0f, 1.0f), random.Float(-1.0f, 1.0f), random.Float(-1.0f, 1.0f));
						planeEdits[i] = terrain::MakeBoxEdit(random.Range(2) == 0 ? terrain::EditType::BOX_DIG : terrain::EditType::BOX_FILL, planeEdits[i].center,
							normal, random.Float(-3.0f, 3.0f), random.Float(0.3f, 2.0f), random.Float(0.3f, 2.0f), (terrain::Material)random.Range((u32)terrain::Material::_COUNT));
					}
					SAVE_CHECK(terrain::WriteEdits(filePath, 42, planeEdits));
					SAVE_CHECK(terrain::ReadEdits(filePath, 42, read) && EditsEqual(read, planeEdits));
				}

				// Damage falls back to the backup
				SAVE_CHECK(terrain::WriteEdits(filePath, 42, edits));
				std::vector<terrain::Edit> moreEdits = edits;
				moreEdits.push_back(edits[0]);
				SAVE_CHECK(terrain::WriteEdits(filePath, 42, moreEdits));
				std::vector<u8> bytes;
				ReadFileBytes(filePath, bytes);
				std::vector<u8> damaged = bytes;
				SAVE_CHECK(bytes.size() > 100);
				if (!damaged.empty())
				{
					damaged[damaged.size() / 2] ^= 0x04;
				}
				WriteFileAtomic(filePath, damaged.data(), damaged.size(), false);
				SAVE_CHECK(terrain::ReadEdits(filePath, 42, read) && EditsEqual(read, edits));

				// Random damage never crashes
				QuietLog quiet;
				Platform::DeleteFile(filePath + ".bak", false);
				for (u32 i = 0; i < 300; ++i)
				{
					damaged = bytes;
					Mutate(damaged, random);
					WriteFileAtomic(filePath, damaged.data(), damaged.size(), false);
					terrain::ReadEdits(filePath, 42, read);
				}
				// (Including old format files)
				WriteEditsV1(filePath, 42, edits);
				ReadFileBytes(filePath, bytes);
				for (u32 i = 0; i < 300; ++i)
				{
					damaged = bytes;
					Mutate(damaged, random);
					WriteFileAtomic(filePath, damaged.data(), damaged.size(), false);
					terrain::ReadEdits(filePath, 42, read);
				}
				TestDirectory();
			}

			static bool CopyFixture(const std::string& fixtureDirectory, const std::string& fileName, const std::string& directory)
			{
				std::vector<u8> bytes;
				return ReadFileBytes(fixtureDirectory + fileName, bytes) && WriteFileAtomic(directory + fileName, bytes.data(), bytes.size(), false);
			}

			// Checks an edits file read from a real save, & that it survives being rewritten
			static void CheckFixtureEdits(TestContext& ctx, const std::string& filePath, i32 seed, u32 expectedCount)
			{
				std::vector<terrain::Edit> read;
				SAVE_CHECK(terrain::ReadEdits(filePath, seed, read) && read.size() == expectedCount);
				{
					QuietLog quiet; // (Logs that the file is damaged)
					SAVE_CHECK(!terrain::ReadEdits(filePath, seed + 1, read));
				}
				std::vector<terrain::Edit> original;
				terrain::ReadEdits(filePath, seed, original);
				SAVE_CHECK(terrain::WriteEdits(filePath, seed, original));
				SAVE_CHECK(terrain::ReadEdits(filePath, seed, read) && EditsEqual(read, original));
			}

			// Real saves written by earlier versions of the game (in resources/tests/save_files/) must still load
			static void TestFixtures(TestContext& ctx)
			{
				const std::string fixtures = RelativePathToAbsolute(RESOURCE_DIRECTORY "tests/save_files/");

				// A world from before binary saves: world.json & version 1 terrain edits
				{
					const std::string fixtureDirectory = fixtures + "legacy_world/";
					const std::string directory = TestDirectory();
					SAVE_CHECK(CopyFixture(fixtureDirectory, LEGACY_WORLD_FILE_NAME, directory));
					SAVE_CHECK(CopyFixture(fixtureDirectory, "terrain_1336491604.edits", directory));

					JSONObject fromText;
					SAVE_CHECK(ReadWorldFile(directory, fromText, false) == WorldFileSource::LEGACY_JSON);
					SAVE_CHECK(fromText.GetString("name") == "World 2" && fromText.GetInt("seed") == 1336491604 && fromText.HasField("player") && fromText.HasField("inventory"));
					SAVE_CHECK(WriteWorldFile(directory, fromText));
					JSONObject fromBinary;
					SAVE_CHECK(ReadWorldFile(directory, fromBinary, false) == WorldFileSource::BINARY);
					std::string difference;
					SAVE_CHECK(JSONEquivalent(SortedFields(fromText), SortedFields(fromBinary), &difference));
					if (!difference.empty())
					{
						PrintError("  %s\n", difference.c_str());
					}
					CheckFixtureEdits(ctx, directory + "terrain_1336491604.edits", 1336491604, 561);
				}

				// A binary world & version 2 terrain edits. expected.json is what it decoded to when it was added
				{
					const std::string fixtureDirectory = fixtures + "binary_world/";
					const std::string directory = TestDirectory();
					SAVE_CHECK(CopyFixture(fixtureDirectory, WORLD_FILE_NAME, directory));
					SAVE_CHECK(CopyFixture(fixtureDirectory, "terrain_80177481.edits", directory));

					JSONObject world;
					SAVE_CHECK(ReadWorldFile(directory, world, false) == WorldFileSource::BINARY);
					JSONObject expected;
					if (!FileExists(fixtureDirectory + "expected.json"))
					{
						// (New fixture: write out what it decodes to now, to check by hand & commit)
						WriteFile(fixtureDirectory + "expected.json", world.ToString(), false);
						SAVE_CHECK(!"binary_world/expected.json was missing");
					}
					SAVE_CHECK(JSONParser::ParseFromFile(fixtureDirectory + "expected.json", expected));
					std::string difference;
					SAVE_CHECK(JSONEquivalent(SortedFields(expected), SortedFields(world), &difference));
					if (!difference.empty())
					{
						PrintError("  %s\n", difference.c_str());
					}

					JSONObject metadata;
					SAVE_CHECK(ReadWorldFile(directory, metadata, true) == WorldFileSource::BINARY);
					SAVE_CHECK(metadata.HasField("name") && metadata.HasField("seed") && !metadata.HasField("player") && !metadata.HasField("objects"));

					// Resaving with the current version reads back the same
					SAVE_CHECK(WriteWorldFile(directory, world));
					JSONObject resaved;
					SAVE_CHECK(ReadWorldFile(directory, resaved, false) == WorldFileSource::BINARY);
					SAVE_CHECK(JSONEquivalent(SortedFields(expected), SortedFields(resaved)));
					CheckFixtureEdits(ctx, directory + "terrain_80177481.edits", 80177481, 145);
				}
				TestDirectory();
			}

			// Old (text) vs new (binary) format: size & time to save & load
			static void Benchmark(TestRandom& random)
			{
				const std::string directory = TestDirectory();
				auto Best = [](u32 runs, const std::function<void()>& func)
				{
					ms best = 1e9f;
					for (u32 i = 0; i < runs; ++i)
					{
						const u64 start = Ticks();
						func();
						best = glm::min(best, MsSince(start));
					}
					return best;
				};
				auto FileSize = [](const std::string& filePath)
				{
					std::ifstream file(filePath, std::ios::binary | std::ios::ate);
					return file ? (u32)file.tellg() : 0u;
				};

				// The largest saved world, or a scene, as a template
				JSONObject world;
				size_t largestSize = 0;
				for (const std::string& filePath : FindJSONFiles())
				{
					const u32 size = FileSize(filePath);
					JSONObject tree;
					if ((EndsWith(filePath, "world.json") || EndsWith(filePath, "world.json.bak")) && size > largestSize && JSONParser::ParseFromFile(filePath, tree))
					{
						largestSize = size;
						world = tree;
					}
				}
				std::vector<JSONObject> objectPool;
				world.TryGetObjectArray("objects", objectPool);
				if (objectPool.empty())
				{
					JSONObject scene;
					if (JSONParser::ParseFromFile(SCENE_DEFAULT_DIRECTORY "scene_world.json", scene))
					{
						scene.TryGetObjectArray("objects", objectPool);
					}
				}

				Print("  World saves (best of 5)  |  objects | text KB | binary KB | text save ms | binary save ms | text load ms | binary load ms | list (metadata) ms\n");
				const u32 objectCounts[] = { 0, 1, 500, 5000 };
				for (u32 objectCount : objectCounts)
				{
					JSONObject scaled = world;
					std::vector<JSONObject> objects;
					const u32 count = objectCount == 1 ? (u32)objectPool.size() : objectCount;
					for (u32 i = 0; i < count && !objectPool.empty(); ++i)
					{
						JSONObject object = objectPool[i % objectPool.size()];
						for (JSONField& field : object.fields)
						{
							if (field.label == "id")
							{
								field.value = JSONValue(GUID(random.Next(), random.Next()).ToString());
							}
							else if (field.label == "transform" && field.value.type == ValueType::OBJECT)
							{
								for (JSONField& transformField : field.value.objectValue.fields)
								{
									if (transformField.label == "pos" && transformField.value.type == ValueType::VEC3)
									{
										transformField.value.vecValue += glm::vec4(random.Float(-3000.0f, 3000.0f), random.Float(-50.0f, 50.0f), random.Float(-3000.0f, 3000.0f), 0.0f);
									}
								}
							}
						}
						objects.push_back(object);
					}
					for (JSONField& field : scaled.fields)
					{
						if (field.label == "objects")
						{
							field.value = JSONValue(objects);
						}
					}

					const std::string textPath = directory + "world.json";
					const ms textSave = Best(5, [&]() { WriteFile(textPath, scaled.ToString(), false); });
					const ms textLoad = Best(5, [&]() { JSONObject tree; JSONParser::ParseFromFile(textPath, tree); });
					const u32 textSize = FileSize(textPath);
					Platform::DeleteFile(textPath, false);
					const ms binarySave = Best(5, [&]() { WriteWorldFile(directory, scaled); });
					const ms binaryLoad = Best(5, [&]() { JSONObject tree; ReadWorldFile(directory, tree, false); });
					const ms binaryList = Best(5, [&]() { JSONObject tree; ReadWorldFile(directory, tree, true); });
					const u32 binarySize = FileSize(directory + WORLD_FILE_NAME);
					Print("  %24s | %8u | %7.1f | %9.1f | %12.2f | %14.2f | %12.2f | %14.2f | %.2f\n", objectCount == 1 ? "(largest saved world)" : "", count,
						textSize / 1024.0f, binarySize / 1024.0f, textSave, binarySave, textLoad, binaryLoad, binaryList);
					TestDirectory();
				}

				Print("  Terrain edits (best of 5) |   edits | v1 KB | v2 KB | v1 save ms | v2 save ms | v1 load ms | v2 load ms\n");
				const u32 editCounts[] = { 561, 10000, 100000 };
				for (u32 editCount : editCounts)
				{
					const std::vector<terrain::Edit> edits = MakeEdits(random, editCount);
					const std::string filePath = directory + "terrain_7.edits";
					std::vector<terrain::Edit> read;
					const ms v1Save = Best(5, [&]() { WriteEditsV1(filePath, 7, edits); });
					const ms v1Load = Best(5, [&]() { terrain::ReadEdits(filePath, 7, read); });
					const u32 v1Size = FileSize(filePath);
					const ms v2Save = Best(5, [&]() { terrain::WriteEdits(filePath, 7, edits); });
					const ms v2Load = Best(5, [&]() { terrain::ReadEdits(filePath, 7, read); });
					const u32 v2Size = FileSize(filePath);
					Print("  %25s | %7u | %5.1f | %5.1f | %10.2f | %10.2f | %10.2f | %10.2f\n", "", editCount, v1Size / 1024.0f, v2Size / 1024.0f, v1Save, v2Save, v1Load, v2Load);
					TestDirectory();
				}
			}
		} // namespace

		bool RunSelfTests()
		{
			Print("Save file tests:\n");
			// (Not set yet in --test mode; needed for absolute paths)
			Platform::RetrieveCurrentWorkingDirectory();
			TestContext ctx;
			TestRandom random{ 0x9E3779B97F4A7C15ull };
			TestPrimitives(ctx);
			TestCompression(ctx, random);
			TestContainer(ctx, random);
			TestBinaryJSON(ctx, random);
			TestRealFiles(ctx);
			TestWorldFiles(ctx, random);
			TestTerrainEdits(ctx, random);
			TestFixtures(ctx);
			// FLEX_SAVE_BENCHMARK=<any value>: after the save file tests in --test, also times world saves & loads (best of 5, text vs binary) & prints a table
			if (getenv("FLEX_SAVE_BENCHMARK") != nullptr)
			{
				Benchmark(random);
			}
			Platform::DeleteEmptyDirectory(TestDirectory());
			Print("Save file tests: %u failures\n", ctx.failures);
			return ctx.failures == 0;
		}
	} // namespace save
} // namespace flex
