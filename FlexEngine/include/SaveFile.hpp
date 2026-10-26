#pragma once

#include "JSONTypes.hpp"

// Binary save files
//
// Every binary save shares one container layout (little endian):
//   Header (24 bytes): magic "FLXB" u32, container version u16, header flags u16, file type (fourcc) u32,
//                      file version u32, section count u32, crc32 of the preceding 20 bytes u32
//   Then per section:  tag (fourcc) u32, flags u32, stored size u32, raw size u32, crc32 of the raw bytes u32,
//                      followed by the stored bytes (zlib compressed when SECTION_ZLIB is set)
// Readers skip sections they don't know & tolerate missing ones, so formats can grow without breaking old saves.
// Nothing read from disk is trusted: every size & count is bounds checked, and each section's crc is verified.
// Files are written to a temporary file first, then renamed over the old one, so a crash mid-save can't lose data.
//
// Sections flagged SECTION_JSON_FIELDS hold a "binary JSON" object: a compact encoding of a JSONObject that decodes
// to the same tree JSONParser would produce from that object's text (so existing JSON loading code can be reused).

namespace flex
{
	namespace save
	{
		constexpr u32 FourCC(char a, char b, char c, char d)
		{
			return (u32)(u8)a | ((u32)(u8)b << 8) | ((u32)(u8)c << 16) | ((u32)(u8)d << 24);
		}

		static const u32 CONTAINER_MAGIC = FourCC('F', 'L', 'X', 'B');
		static const u16 CONTAINER_VERSION = 1;
		static const u32 MAX_SECTION_COUNT = 4096;
		static const u32 MAX_SECTION_SIZE = 512u * 1024u * 1024u;

		enum SectionFlags : u32
		{
			SECTION_ZLIB = 1 << 0,			// Stored bytes are a zlib stream
			SECTION_JSON_FIELDS = 1 << 1,	// Raw bytes are a binary JSON object whose fields belong to the file's root object
			// Bits 0-15 change how a section must be read: sections with unknown bits there are skipped
			SECTION_REQUIRED_FLAGS_MASK = 0xFFFF,
			SECTION_KNOWN_FLAGS = SECTION_ZLIB | SECTION_JSON_FIELDS,
		};

		class Writer
		{
		public:
			void Bytes(const void* data, size_t size);
			template<typename T>
			void Write(const T& value)
			{
				static_assert(std::is_trivially_copyable<T>::value, "Only plain values can be written");
				Bytes(&value, sizeof(T));
			}
			void VarU64(u64 value);
			void String(const std::string& str);

			std::vector<u8> bytes;
		};

		// Bounds checked. Any failed read makes every following read fail too, so callers can check once at the end.
		class Reader
		{
		public:
			Reader(const u8* data, size_t size) : m_Data(data), m_Size(data != nullptr ? size : 0) {}

			bool Bytes(void* out, size_t size);
			template<typename T>
			bool Read(T& outValue)
			{
				static_assert(std::is_trivially_copyable<T>::value, "Only plain values can be read");
				return Bytes(&outValue, sizeof(T));
			}
			bool VarU64(u64& outValue);
			// A count of elements which each take at least minBytesPerElement bytes, so it can't exceed what's left
			bool Count(u32& outCount, size_t minBytesPerElement);
			bool String(std::string& outStr, size_t maxLength = 1u << 20);
			bool Skip(size_t size);
			bool Fail() { m_bFailed = true; return false; }

			const u8* Current() const { return m_Data + m_Offset; }
			size_t Remaining() const { return m_bFailed ? 0 : m_Size - m_Offset; }
			bool Failed() const { return m_bFailed; }
			bool AtEnd() const { return !m_bFailed && m_Offset == m_Size; }

		private:
			const u8* m_Data;
			size_t m_Size;
			size_t m_Offset = 0;
			bool m_bFailed = false;
		};

		u32 Crc32(const void* data, size_t size, u32 crc = 0);

		// For timing saves & loads (steady clock, in nanoseconds)
		u64 Ticks();
		ms MsSince(u64 ticks);

		// zlib (via stb). Decompress fails unless the output is exactly rawSize bytes.
		bool Compress(const u8* data, size_t size, std::vector<u8>& outCompressed);
		bool Decompress(const u8* data, size_t size, size_t rawSize, std::vector<u8>& outRaw);

		// Groups byte k of every element together, which makes arrays of floats far more compressible
		void ShuffleBytes(const u8* data, size_t elementCount, size_t elementSize, u8* out);
		void UnshuffleBytes(const u8* data, size_t elementCount, size_t elementSize, u8* out);

		bool ReadFileBytes(const std::string& filePath, std::vector<u8>& outBytes, size_t maxSize = 1024ull * 1024ull * 1024ull);
		// Writes to filePath + ".tmp", flushes it to disk, then renames it over filePath. When bKeepBackup is set the
		// previous file is kept as filePath + ".bak".
		bool WriteFileAtomic(const std::string& filePath, const void* data, size_t size, bool bKeepBackup);

		class FileWriter
		{
		public:
			FileWriter(u32 fileType, u32 fileVersion) : m_FileType(fileType), m_FileVersion(fileVersion) {}

			// Compressed when requested & it saves at least an eighth of the size
			void AddSection(u32 tag, const std::vector<u8>& raw, u32 flags, bool bCompress);
			void AddJSONSection(u32 tag, const JSONObject& fields, bool bCompress);

			std::vector<u8> Build() const;
			bool Save(const std::string& filePath, bool bKeepBackup, size_t* outSize = nullptr) const;

		private:
			struct Section
			{
				u32 tag;
				u32 flags;
				u32 rawSize;
				u32 rawCrc;
				std::vector<u8> stored;
			};

			u32 m_FileType;
			u32 m_FileVersion;
			std::vector<Section> m_Sections;
		};

		enum class LoadResult
		{
			OK,
			MISSING,				// No file (or it couldn't be opened)
			NOT_CONTAINER,			// Not a binary save (e.g. an older format)
			WRONG_TYPE,				// A binary save of another kind
			UNSUPPORTED_VERSION,	// A newer container layout
			CORRUPT,				// Truncated or damaged
		};

		const char* LoadResultToString(LoadResult result);

		class FileReader
		{
		public:
			// Reads the header & section table. Sections are only decompressed (& their crcs checked) when requested.
			// When onlyTags is given, other sections' bytes are skipped without being read.
			LoadResult Load(const std::string& filePath, u32 expectedType, const std::vector<u32>* onlyTags = nullptr);
			LoadResult Parse(const u8* data, size_t size, u32 expectedType);

			u32 FileVersion() const { return m_FileVersion; }
			bool HasSection(u32 tag) const;
			u32 SectionFlags(u32 tag) const;
			// False if the section is missing, damaged, or uses flags this version doesn't understand
			bool GetSection(u32 tag, std::vector<u8>& outRaw) const;
			// Merges the fields of every readable SECTION_JSON_FIELDS section into outObject. Returns false if any is
			// damaged.
			bool GetJSONFields(JSONObject& outObject) const;

			std::vector<u32> Tags() const;

		private:
			struct Section
			{
				u32 tag;
				u32 flags;
				u32 rawSize;
				u32 rawCrc;
				std::vector<u8> stored;
			};

			bool GetSection(const Section& section, std::vector<u8>& outRaw) const;

			u32 m_FileVersion = 0;
			std::vector<Section> m_Sections;
		};

		// Binary JSON (see top of file)
		void EncodeJSON(const JSONObject& object, std::vector<u8>& outBytes);
		bool DecodeJSON(const u8* data, size_t size, JSONObject& outObject);
		// Whether two trees hold the same data (floats compared with a tolerance, as text saves round them)
		bool JSONEquivalent(const JSONObject& a, const JSONObject& b, std::string* outDifference = nullptr);

		// Worlds: saved/worlds/<name>/world.bin. Older worlds only have world.json, which is read instead (& left as
		// world.json.bak once the world has been saved in the binary format).
		static const u32 WORLD_FILE_TYPE = FourCC('W', 'R', 'L', 'D');
		static const u32 WORLD_FILE_VERSION = 1;
		static const char* const WORLD_FILE_NAME = "world.bin";
		static const char* const LEGACY_WORLD_FILE_NAME = "world.json";

		enum class WorldFileSource
		{
			NONE,
			BINARY,
			BINARY_BACKUP,
			LEGACY_JSON,
		};

		struct WorldFileStats
		{
			size_t sizeBytes = 0;
			ms encodeMs = 0.0f;
			ms ioMs = 0.0f;
		};

		bool WorldFileExists(const std::string& directory);
		// worldObj is the world's root object, as it would be written to world.json. Small fields (name, seed, play
		// time, stats, ...) go in a "META" section, which is all that's read when only listing worlds; any field can
		// be added without changing the format.
		// bAsync writes the file on another thread (it's encoded before returning, so worldObj needn't be kept). Reading
		// a world waits for it; call WaitForPendingWrites before deleting worlds or exiting.
		bool WriteWorldFile(const std::string& directory, const JSONObject& worldObj, WorldFileStats* outStats = nullptr, bool bAsync = false);
		void WaitForPendingWrites();
		// bMetadataOnly skips everything but the small fields (for listing worlds)
		WorldFileSource ReadWorldFile(const std::string& directory, JSONObject& outWorldObj, bool bMetadataOnly, WorldFileStats* outStats = nullptr);
		const char* WorldFileSourceToString(WorldFileSource source);

		// Unit tests (round trips, corruption/truncation fuzzing, equivalence with the JSON text path & timings)
		bool RunSelfTests();
	} // namespace save
} // namespace flex
