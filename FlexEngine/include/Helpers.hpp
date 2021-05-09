#pragma once

#include "Types.hpp"

typedef int ImGuiInputTextFlags;
typedef int ImGuiColorEditFlags;
struct GLFWimage;

namespace flex
{
	class Transform;
	class StringBuilder;

	// TODO: Many of the functions in this file would benefit from unit tests

	static const i32 DEFAULT_FLOAT_PRECISION = 2;

	GLFWimage LoadGLFWimage(const std::string& filePath, i32 requestedChannelCount = 3, bool bFlipVertically = false, u32* outChannelCount = nullptr);
	void DestroyGLFWimage(GLFWimage& image);

	// TODO: Move to Platform layer
	bool FileExists(const std::string& filePath);

	bool ReadFile(const std::string& filePath, std::string& outFileContents, bool bBinaryFile);
	bool ReadFile(const std::string& filePath, std::vector<char>& vec, bool bBinaryFile);

	bool WriteFile(const std::string& filePath, const std::string& fileContents, bool bBinaryFile);
	bool WriteFile(const std::string& filePath, const std::vector<char>& vec, bool bBinaryFile);

	bool OpenJSONFileDialog(const std::string& windowTitle, const std::string& absoluteDirectory, std::string& outSelectedAbsFilePath);

	/*
	* Reads in a .wav file and fills in given values according to file contents
	* Returns true if reading and parsing succeeded
	*/
	bool ParseWAVFile(const std::string& filePath, i32* format, u8** data, u32* size, u32* freq, StringBuilder& outErrorStr);

	// Removes all content before final '/' or '\'
	FLEX_NO_DISCARD std::string StripLeadingDirectories(const std::string& filePath);

	// Removes all content after final '/' or '\'
	// NOTE: If path describes a directory and doesn't end in a slash, final directory will be removed
	FLEX_NO_DISCARD std::string ExtractDirectoryString(const std::string& filePath);

	// Removes all chars after first '.' occurrence
	FLEX_NO_DISCARD std::string StripFileType(const std::string& filePath);

	// Removes all chars before first '.' occurrence
	FLEX_NO_DISCARD std::string ExtractFileType(const std::string& filePath);

	// Strips leading and trailing whitespace
	FLEX_NO_DISCARD std::string Trim(const std::string& str);

	FLEX_NO_DISCARD std::string TrimLeadingWhitespace(const std::string& str);

	FLEX_NO_DISCARD std::string TrimTrailingWhitespace(const std::string& str);

	FLEX_NO_DISCARD std::vector<std::string> Split(const std::string& str, char delim);
	FLEX_NO_DISCARD std::vector<std::string> Split(const std::string& str, const char* delim);
	// Includes blank entries for subsequent delims
	// (e.g. "\n\n\n" will return a vector of length 3, while Strip will return an empty vector)
	FLEX_NO_DISCARD std::vector<std::string> SplitNoStrip(const std::string& str, char delim);
	FLEX_NO_DISCARD std::vector<std::string> SplitNoStrip(const std::string& str, const char* delim);

	/*
	 * Returns the index of the first character which isn't a number
	 * of letter (or -1 if none exist) starting from offset
	 */
	FLEX_NO_DISCARD i32 NextNonAlphaNumeric(const std::string& str, i32 offset);

	FLEX_NO_DISCARD bool NearlyEquals(real a, real b, real threshold);
	FLEX_NO_DISCARD bool NearlyEquals(const glm::vec2& a, const glm::vec2& b, real threshold);
	FLEX_NO_DISCARD bool NearlyEquals(const glm::vec3& a, const glm::vec3& b, real threshold);
	FLEX_NO_DISCARD bool NearlyEquals(const glm::vec4& a, const glm::vec4& b, real threshold);
	FLEX_NO_DISCARD bool NearlyEquals(const glm::quat& a, const glm::quat& b, real threshold);

	FLEX_NO_DISCARD glm::quat MoveTowards(const glm::quat& a, const glm::quat& b, real delta);
	FLEX_NO_DISCARD glm::vec2 MoveTowards(const glm::vec2& a, const glm::vec2& b, real delta);
	FLEX_NO_DISCARD glm::vec3 MoveTowards(const glm::vec3& a, const glm::vec3& b, real delta);
	FLEX_NO_DISCARD real MoveTowards(const real& a, const real b, real delta);

	FLEX_NO_DISCARD real Lerp(real a, real b, real t);
	FLEX_NO_DISCARD glm::vec2 Lerp(const glm::vec2& a, const glm::vec2& b, real t);
	FLEX_NO_DISCARD glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, real t);
	FLEX_NO_DISCARD glm::vec4 Lerp(const glm::vec4& a, const glm::vec4& b, real t);

	FLEX_NO_DISCARD u32 Pack2FloatToU32(real f1, real f2);
	void UnpackU32To2Float(u32 u1, real* outF1, real* outF2);

	/* Interpret 4 bytes starting at ptr as an unsigned 32-bit int */
	FLEX_NO_DISCARD u32 Parse32u(const char* ptr);
	/* Interpret 2 bytes starting at ptr as an unsigned 16-bit int */
	FLEX_NO_DISCARD u16 Parse16u(const char* ptr);

	FLEX_NO_DISCARD bool ParseBool(const std::string& intStr);

	FLEX_NO_DISCARD i32 ParseInt(const std::string& intStr);
	FLEX_NO_DISCARD u32 ParseUInt(const std::string& intStr);
	FLEX_NO_DISCARD i64 ParseLong(const std::string& intStr);
	FLEX_NO_DISCARD u64 ParseULong(const std::string& intStr);

	/* Parses a single float, returns -1.0f if incorrectly formatted */
	FLEX_NO_DISCARD real ParseFloat(const std::string& floatStr);

	/* Parses a comma separated list of 2 floats */
	FLEX_NO_DISCARD glm::vec2 ParseVec2(const std::string& vecStr);

	/* Parses a comma separated list of 3 floats */
	FLEX_NO_DISCARD glm::vec3 ParseVec3(const std::string& vecStr);

	/*
	* Parses a comma separated list of 3 or 4 floats
	* If defaultW is non-negative and no w component exists in the string defaultW will be used
	* If defaultW is negative then an error will be generated if the string doesn't
	* contain 4 components
	*/
	FLEX_NO_DISCARD glm::vec4 ParseVec4(const std::string& vecStr, real defaultW = 1.0f);

	FLEX_NO_DISCARD glm::quat ParseQuat(const std::string& quatStr);

	FLEX_NO_DISCARD u32 CountSetBits(u32 bits);

	FLEX_NO_DISCARD bool IsNanOrInf(real val);
	FLEX_NO_DISCARD bool IsNanOrInf(const glm::vec2& vec);
	FLEX_NO_DISCARD bool IsNanOrInf(const glm::vec3& vec);
	FLEX_NO_DISCARD bool IsNanOrInf(const glm::vec4& vec);
	FLEX_NO_DISCARD bool IsNanOrInf(const glm::quat& quat);

	FLEX_NO_DISCARD real RoundToNearestPowerOfTwo(real num);
	FLEX_NO_DISCARD u64 NextPowerOfTwo(u64 x);
	FLEX_NO_DISCARD u32 NextPowerOfTwo(u32 x);

	FLEX_NO_DISCARD std::string GetIncrementedPostFixedStr(const std::string& namePrefix, const std::string& defaultName);

	void PadEnd(std::string& str, i32 minLen, char pad);
	void PadStart(std::string& str, i32 minLen, char pad);
	// String will be padded to be at least minChars long (excluding a leading '-' for negative numbers)
	FLEX_NO_DISCARD std::string IntToString(i32 i, u16 minChars = 0, char pad = '0');
	FLEX_NO_DISCARD std::string UIntToString(u32 i, u16 minChars = 0, char pad = '0');
	FLEX_NO_DISCARD std::string LongToString(i64 i, u16 minChars = 0, char pad = '0');
	FLEX_NO_DISCARD std::string ULongToString(u64 i, u16 minChars = 0, char pad = '0');

	FLEX_NO_DISCARD std::string FloatToString(real f, i32 precision = DEFAULT_FLOAT_PRECISION);

	FLEX_NO_DISCARD std::string VecToString(const glm::vec2& vec, i32 precision = DEFAULT_FLOAT_PRECISION);
	FLEX_NO_DISCARD std::string VecToString(const glm::vec3& vec, i32 precision = DEFAULT_FLOAT_PRECISION);
	FLEX_NO_DISCARD std::string VecToString(const glm::vec4& vec, i32 precision = DEFAULT_FLOAT_PRECISION);

	FLEX_NO_DISCARD std::string Vec2ToString(real* data, i32 precision = DEFAULT_FLOAT_PRECISION);
	FLEX_NO_DISCARD std::string Vec3ToString(real* data, i32 precision = DEFAULT_FLOAT_PRECISION);
	FLEX_NO_DISCARD std::string Vec4ToString(real* data, i32 precision = DEFAULT_FLOAT_PRECISION);

	FLEX_NO_DISCARD std::string VecToString(real x, real y, i32 precision = DEFAULT_FLOAT_PRECISION);
	FLEX_NO_DISCARD std::string VecToString(real x, real y, real z, i32 precision = DEFAULT_FLOAT_PRECISION);
	FLEX_NO_DISCARD std::string VecToString(real x, real y, real z, real w, i32 precision = DEFAULT_FLOAT_PRECISION);

	FLEX_NO_DISCARD std::string QuatToString(const glm::quat& quat, i32 precision = DEFAULT_FLOAT_PRECISION);
	FLEX_NO_DISCARD std::string QuatToString(real x, real y, real z, real w, i32 precision = DEFAULT_FLOAT_PRECISION);
	FLEX_NO_DISCARD std::string QuatToString(real* data, i32 precision = DEFAULT_FLOAT_PRECISION);

	void CopyVec3ToClipboard(const glm::vec3& vec);
	void CopyVec4ToClipboard(const glm::vec4& vec);

	void CopyColourToClipboard(const glm::vec3& col);
	void CopyColourToClipboard(const glm::vec4& col);

	void CopyTransformToClipboard(Transform* transform);
	bool PasteTransformFromClipboard(Transform* transform);

	FLEX_NO_DISCARD glm::vec3 PasteColour3FromClipboard();
	FLEX_NO_DISCARD glm::vec4 PasteColour4FromClipboard();

	bool PointOverlapsTriangle(const glm::vec2& point, const glm::vec2& tri0, const glm::vec2& tri1, const glm::vec2& tri2);
	// Return closest point on triangle (either at vertex, at edge, or on face) and return signed distance
	// to that point (negative inside, positive outside)
	real SignedDistanceToTriangle(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, glm::vec3& outClosestPoint);

	FLEX_NO_DISCARD char* ToLower(char* str);
	std::string& ToLower(std::string& str);
	std::string& ToUpper(std::string& str);

	FLEX_NO_DISCARD bool StartsWith(const std::string& str, const std::string& start);
	FLEX_NO_DISCARD bool EndsWith(const std::string& str, const std::string& end);
	FLEX_NO_DISCARD std::string RemoveEndIfPresent(const std::string& str, const std::string& end);

	// Returns the number str ends with or -1 if last char isn't numeral
	// outNumNumericalChars will be set to the number of chars in the num (e.g. "001" => 3)
	FLEX_NO_DISCARD i32 GetNumberEndingWith(const std::string& str, i16& outNumNumericalChars);

	FLEX_NO_DISCARD std::string ReplaceBackSlashesWithForward(std::string str);
	FLEX_NO_DISCARD std::string RelativePathToAbsolute(std::string relativePath);

	FLEX_NO_DISCARD std::string EnsureTrailingSlash(const std::string& str);

	FLEX_NO_DISCARD std::string Replace(const std::string& str, const std::string& pattern, const std::string& replacement);
	FLEX_NO_DISCARD std::string Replace(const std::string& str, char pattern, char replacement);

	// Returns random value in range [min, max)
	FLEX_NO_DISCARD i32 RandomInt(i32 min, i32 max);

	// Returns random value in range [min, max)
	FLEX_NO_DISCARD real RandomFloat(real min, real max);

	// TODO: Add tests! Unused!
	bool Base64Encode(const u8* src, char* dst, size_t len);
	bool Base64Decode(const void* src, u8* dst, const size_t len);

	void ByteCountToString(char buf[], u32 bufSize, u32 bytes);

	FLEX_NO_DISCARD real MinComponent(const glm::vec2& vec);
	FLEX_NO_DISCARD real MinComponent(const glm::vec3& vec);
	FLEX_NO_DISCARD real MinComponent(const glm::vec4& vec);

	FLEX_NO_DISCARD real MaxComponent(const glm::vec2& vec);
	FLEX_NO_DISCARD real MaxComponent(const glm::vec3& vec);
	FLEX_NO_DISCARD real MaxComponent(const glm::vec4& vec);

	FLEX_NO_DISCARD inline real Saturate(real val)
	{
		return glm::clamp(val, 0.0f, 1.0f);
	}

	template<typename T>
	FLEX_NO_DISCARD T Saturate(T val)
	{
		return glm::clamp(val, T(0), T(1));
	}

	FLEX_NO_DISCARD glm::vec2 Floor(const glm::vec2& p);
	FLEX_NO_DISCARD glm::vec2 Fract(const glm::vec2& p);

	FLEX_NO_DISCARD glm::vec3 Floor(const glm::vec3& p);
	FLEX_NO_DISCARD glm::vec3 Fract(const glm::vec3& p);

	static const unsigned char hex_table[17] = "0123456789ABCDEF";

	FLEX_NO_DISCARD inline u8 DecimalToHex(u8 dec)
	{
		return hex_table[dec];
	}

	FLEX_NO_DISCARD inline u8 DecimalFromHex(char hex)
	{
		if (hex <= '9')
		{
			return (u8)(hex - '0');
		}

		return 10 + (u8)(hex - 'A');
	}

	// Returns monotonically increasing ID with each call (for a
	// globally-unique value use Platform::GenerateGUID instead)
	FLEX_NO_DISCARD u32 GenerateUID();

	FLEX_NO_DISCARD real SmoothStep01(real t);
	FLEX_NO_DISCARD real SmootherStep01(real t);
	FLEX_NO_DISCARD real SmootherStep(real a, real b, real t);

	static const u64 FNV1_64_Prime = 1099511628211u;
	static const u64 FNV1_64_Offset = 14695981039346656037u;

	// FNV-1a Hash http://isthe.com/chongo/tech/comp/fnv/
	constexpr u64 Hash(const char* str)
	{
		u64 result = FNV1_64_Offset;

		while (*str != 0)
		{
			char c = *str++;
			result = result ^ c;
			result = result * FNV1_64_Prime;
		}

		return result;
	}

	template<class T>
	inline i32 Find(const std::vector<T>& vec, const T& t)
	{
		T* vecData = (T*)vec.data();
		i32 vecLen = (i32)vec.size();
		for (i32 i = 0; i < vecLen; ++i)
		{
			if (*vecData == t)
			{
				return i;
			}
			++vecData;
		}

		return -1;
	}

	template<class T>
	inline typename std::vector<T>::const_iterator FindIter(const std::vector<T>& vec, const T& t)
	{
		auto vecEnd = vec.end();
		for (typename std::vector<T>::const_iterator iter = vec.begin(); iter != vecEnd; ++iter)
		{
			if (*iter == t)
			{
				return iter;
			}
		}

		return vec.end();
	}

	template<class T>
	inline bool Contains(const std::vector<T>& vec, const T& t)
	{
		return Find(vec, t) != -1;
	}

	template<class Key, class Value, class Comp>
	inline bool Contains(const std::map<Key, Value, Comp>& map, const Key& key)
	{
		auto iter = map.find(key);
		return iter != map.end();
	}

	template<class Value, class Comp>
	inline bool Contains(const std::set<Value, Comp>& set, const Value& val)
	{
		auto iter = set.find(val);
		return iter != set.end();
	}

	bool Contains(const std::vector<const char*>& vec, const char* val);
	bool Contains(const char* arr[], u32 arrLen, const char* val);
	bool Contains(const std::string& str, const std::string& pattern);
	bool Contains(const std::string& str, char pattern);

	template<typename T>
	inline bool Erase(std::vector<T>& vec, const T& t)
	{
		i32 index = Find(vec, t);
		if (index != -1)
		{
			vec.erase(index);
			return true;
		}
		return false;
	}

	template<typename T, class I>
	inline bool Erase(std::set<T, I>& set, const T& t)
	{
		auto iter = set.find(t);
		if (iter != set.end())
		{
			set.erase(iter);
			return true;
		}
		return false;
	}

	template<typename T>
	i32 IndexOf(const std::vector<T>& vec, T val)
	{
		for (i32 i = 0; i < (i32)vec.size(); ++i)
		{
			if (vec[i] == val) {
				return i;
			}
		}
		return -1;
	}

	template<class T>
	const T& PickRandomFrom(const std::vector<T>& vec)
	{
		return vec[RandomInt(0, (i32)vec.size())];
	}

	FLEX_NO_DISCARD i32 RoundUp(i32 val, i32 alignment);

	// Returns true if value changed
	bool DoImGuiRotationDragFloat3(const char* label, glm::vec3& rotation, glm::vec3& outCleanedRotation);

	// UNUSED
	void CalculateOrthonormalBasis(const glm::vec3& n, glm::vec3& b1, glm::vec3& b2);

	enum class ImageFormat
	{
		JPG,
		TGA,
		PNG,
		BMP
	};

	bool SaveImage(const std::string& absoluteFilePath, ImageFormat imageFormat, i32 width, i32 height, i32 channelCount, u8* data, bool bFlipVertically = false);

	struct HDRImage
	{
		bool Load(const std::string& hdrFilePath, i32 requestedChannelCount, bool bFlipVertically);
		void Free();

		u32 width;
		u32 height;
		u32 channelCount;
		std::string filePath;
		real* pixels;
	};

	// Stores text render commands issued during the
	// frame to later be converted to "TextVertex"s
	struct TextCache
	{
	public:
		// Screen-space constructor
		TextCache(const std::string& text, AnchorPoint anchor, const glm::vec2& position,
			const glm::vec4& col, real xSpacing, real scale);
		// World-space constructor
		TextCache(const std::string& text, const glm::vec3& position, const glm::quat& rot,
			const glm::vec4& col, real xSpacing, real scale);

		std::string str;
		AnchorPoint anchor;
		glm::vec3 pos;
		glm::quat rot;
		glm::vec4 colour;
		real xSpacing;
		real scale;

	private:
		//TextCache& operator=(const TextCache &tmp);

	};

	struct Vec2iCompare
	{
		bool operator()(const glm::vec2i& lhs, const glm::vec2i& rhs) const;
	};

	// TODO: Move to separate file
	template<class T>
	struct ThreadSafeArray
	{
		ThreadSafeArray()
		{
		}

		explicit ThreadSafeArray<T>(u32 inSize)
		{
			size = inSize;
			t = new T[inSize];
		}

		~ThreadSafeArray()
		{
			delete[] t;
		}

		volatile T& operator[](u32 index) volatile
		{
			return t[index];
		}

		const volatile T& operator[](u32 index) volatile const
		{
			return t[index];
		}

		u32 Size()volatile const
		{
			return size;
		}

		u32 size;
		volatile T* t = nullptr;
	};

	struct WaveThreadData
	{
		void* criticalSection = nullptr;
		volatile bool running = true;
	};

	struct TerrainThreadData
	{
		void* criticalSection = nullptr;
		volatile bool running = true;

	};

	struct AABB2D
	{
		real minX, maxX, minZ, maxZ;

		bool Overlaps(const AABB2D& other)
		{
			return !(other.maxX < minX || other.maxZ < minZ ||
				other.minX > maxX || other.minZ < maxZ);
		}

		bool Contains(const glm::vec2& point)
		{
			return (point.x >= minX && point.x < maxX &&
				point.y >= minZ && point.y < maxZ);
		}

	};

	struct AABB
	{
		// NOTE: ! Order = X Z Y so we can cast directly to AABB2D !
		real minX, maxX, minZ, maxZ, minY, maxY;

		// TODO: Add tests
		bool Overlaps(const AABB& other)
		{
			return !(other.maxX < minX || other.maxY < minY || other.maxZ < minZ ||
				other.minX > maxX || other.minY < maxY || other.minZ < maxZ);
		}

		bool Overlaps(const AABB2D& other)
		{
			return ((AABB2D*)this)->Overlaps(other);
		}

		bool Contains(const glm::vec3& point)
		{
			return (point.x >= minX && point.x < maxX &&
				point.z >= minZ && point.z < maxZ &&
				point.y >= minY && point.y < maxY);
		}

		bool Contains(const glm::vec2& point)
		{
			return ((AABB2D*)this)->Contains(point);
		}

		void DrawDebug(const btVector3& lineColour);
	};

	struct PointTest
	{
		glm::vec3 start;
		glm::vec3 closest;
		real dist;

		static std::vector<PointTest> ComputePointTests(
			const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
			const AABB& sampleBounds, i32 numSamples);
	};

	namespace ImGuiExt
	{
		bool InputUInt(const char* message, u32* v, u32 step = 1, u32 step_fast = 100, ImGuiInputTextFlags flags = 0);
		bool SliderUInt(const char* label, u32* v, u32 v_min, u32 v_max, const char* format = NULL);
		bool DragUInt(const char* label, u32* v, real speed = 1.0f, u32 v_min = 0, u32 v_max = 0, const char* format = "%d");
		bool DragInt16(const char* label, i16* v, real speed = 1.0f, i16 v_min = 0, i16 v_max = 0, const char* format = "%d");
		bool ColorEdit3Gamma(const char* label, real* v, ImGuiColorEditFlags flags = 0);
		bool ColorEdit4Gamma(const char* label, real* v, ImGuiColorEditFlags flags = 0);
	} // namespace ImGuiExt
} // namespace flex
