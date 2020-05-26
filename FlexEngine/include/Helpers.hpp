#pragma once

#include "Graphics/RendererTypes.hpp"

IGNORE_WARNINGS_PUSH
#include <glm/gtx/compatibility.hpp> // For saturate
IGNORE_WARNINGS_POP


namespace flex
{
	// TODO: Many of the functions in this file would benefit from unit tests

	static const i32 DEFAULT_FLOAT_PRECISION = 2;

	GLFWimage LoadGLFWimage(const std::string& filePath, i32 requestedChannelCount = 3, bool bFlipVertically = false, u32* channelCountOut = nullptr);
	void DestroyGLFWimage(GLFWimage& image);

	bool FileExists(const std::string& filePath);

	bool ReadFile(const std::string& filePath, std::string& outFileContents, bool bBinaryFile);
	bool ReadFile(const std::string& filePath, std::vector<char>& vec, bool bBinaryFile);

	bool WriteFile(const std::string& filePath, const std::string& fileContents, bool bBinaryFile);
	bool WriteFile(const std::string& filePath, const std::vector<char>& vec, bool bBinaryFile);

	bool OpenJSONFileDialog(const std::string& windowTitle, const std::string& absoluteDirectory, std::string& outSelectedAbsFilePath);

	// Removes all content before final '/' or '\'
	std::string StripLeadingDirectories(std::string filePath);

	// Removes all content after final '/' or '\'
	// NOTE: If path describes a directory and doesn't end in a slash, final directory will be removed
	std::string ExtractDirectoryString(std::string filePath);

	// Removes all chars after first '.' occurrence
	std::string StripFileType(std::string filePath);

	// Removes all chars before first '.' occurrence
	// TODO: EZ: Test
	std::string ExtractFileType(const std::string& filePath);

	/*
	* Reads in a .wav file and fills in given values according to file contents
	* Returns true if reading and parsing succeeded
	*/
	bool ParseWAVFile(const std::string& filePath, i32* format, u8** data, i32* size, i32* freq);

	std::string TrimStartAndEnd(const std::string& str);

	std::vector<std::string> Split(const std::string& str, char delim);

	/*
	 * Returns the index of the first character which isn't a number
	 * of letter (or -1 if none exist) starting from offset
	 */
	i32 NextNonAlphaNumeric(const std::string& str, i32 offset);

	bool NearlyEquals(real a, real b, real threshold);
	bool NearlyEquals(const glm::vec2& a, const glm::vec2& b, real threshold);
	bool NearlyEquals(const glm::vec3& a, const glm::vec3& b, real threshold);
	bool NearlyEquals(const glm::vec4& a, const glm::vec4& b, real threshold);
	bool NearlyEquals(const glm::quat& a, const glm::quat& b, real threshold);

	glm::quat MoveTowards(const glm::quat& a, const glm::quat& b, real delta);
	glm::vec3 MoveTowards(const glm::vec3& a, const glm::vec3& b, real delta);
	real MoveTowards(const real& a, const real b, real delta);

	real Lerp(real a, real b, real t);
	glm::vec2 Lerp(const glm::vec2& a, const glm::vec2& b, real t);
	glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, real t);
	glm::vec4 Lerp(const glm::vec4& a, const glm::vec4& b, real t);

	u32 Pack2FloatToU32(real f1, real f2);
	void UnpackU32To2Float(u32 u1, real* outF1, real* outF2);

	/* Interpret 4 bytes starting at ptr as an unsigned 32-bit int */
	u32 Parse32u(const char* ptr);
	/* Interpret 2 bytes starting at ptr as an unsigned 16-bit int */
	u16 Parse16u(const char* ptr);

	bool ParseBool(const std::string& intStr);

	i32 ParseInt(const std::string& intStr);

	/* Parses a single float, returns -1.0f if incorrectly formatted */
	real ParseFloat(const std::string& floatStr);

	/* Parses a comma separated list of 2 floats */
	glm::vec2 ParseVec2(const std::string& vecStr);

	/* Parses a comma separated list of 3 floats */
	glm::vec3 ParseVec3(const std::string& vecStr);

	/*
	* Parses a comma separated list of 3 or 4 floats
	* If defaultW is non-negative and no w component exists in the string defaultW will be used
	* If defaultW is negative then an error will be generated if the string doesn't
	* contain 4 components
	*/
	glm::vec4 ParseVec4(const std::string& vecStr, real defaultW = 1.0f);

	glm::quat ParseQuat(const std::string& quatStr);

	u32 CountSetBits(u32 bits);

	bool IsNanOrInf(real val);
	bool IsNanOrInf(const glm::vec2& vec);
	bool IsNanOrInf(const glm::vec3& vec);
	bool IsNanOrInf(const glm::vec4& vec);
	bool IsNanOrInf(const glm::quat& quat);

	real RoundToNearestPowerOfTwo(real num);
	u64 NextPowerOfTwo(u64 x);
	u32 NextPowerOfTwo(u32 x);

	std::string GetIncrementedPostFixedStr(const std::string& namePrefix, const std::string& defaultName);

	void PadEnd(std::string& str, i32 minLen, char pad);
	void PadStart(std::string& str, i32 minLen, char pad);
	// String will be padded to be at least minChars long (excluding a leading '-' for negative numbers)
	std::string IntToString(i32 i, u16 minChars = 0, char pad = '0');

	std::string FloatToString(real f, i32 precision = DEFAULT_FLOAT_PRECISION);

	std::string BoolToString(bool b);

	std::string VecToString(const glm::vec2& vec, i32 precision = DEFAULT_FLOAT_PRECISION);
	std::string VecToString(const glm::vec3& vec, i32 precision = DEFAULT_FLOAT_PRECISION);
	std::string VecToString(const glm::vec4& vec, i32 precision = DEFAULT_FLOAT_PRECISION);

	std::string Vec2ToString(real* data, i32 precision = DEFAULT_FLOAT_PRECISION);
	std::string Vec3ToString(real* data, i32 precision = DEFAULT_FLOAT_PRECISION);
	std::string Vec4ToString(real* data, i32 precision = DEFAULT_FLOAT_PRECISION);

	std::string VecToString(real x, real y, i32 precision = DEFAULT_FLOAT_PRECISION);
	std::string VecToString(real x, real y, real z, i32 precision = DEFAULT_FLOAT_PRECISION);
	std::string VecToString(real x, real y, real z, real w, i32 precision = DEFAULT_FLOAT_PRECISION);

	std::string QuatToString(const glm::quat& quat, i32 precision = DEFAULT_FLOAT_PRECISION);
	std::string QuatToString(real x, real y, real z, real w, i32 precision = DEFAULT_FLOAT_PRECISION);
	std::string QuatToString(real* data, i32 precision = DEFAULT_FLOAT_PRECISION);

	void CopyVec3ToClipboard(const glm::vec3& vec);
	void CopyVec4ToClipboard(const glm::vec4& vec);

	void CopyColorToClipboard(const glm::vec3& col);
	void CopyColorToClipboard(const glm::vec4& col);

	void CopyTransformToClipboard(Transform* transform);
	bool PasteTransformFromClipboard(Transform* transform);

	glm::vec3 PasteColor3FromClipboard();
	glm::vec4 PasteColor4FromClipboard();

	CullFace StringToCullFace(const std::string& str);
	std::string CullFaceToString(CullFace cullFace);

	char* ToLower(char* str);
	std::string& ToLower(std::string& str);
	std::string& ToUpper(std::string& str);

	bool StartsWith(const std::string& str, const std::string& start);
	bool EndsWith(const std::string& str, const std::string& end);

	// Returns the number str ends with or -1 if last char isn't numeral
	// outNumNumericalChars will be set to the number of chars in the num (e.g. "001" => 3)
	i32 GetNumberEndingWith(const std::string& str, i16& outNumNumericalChars);

	const char* GameObjectTypeToString(GameObjectType type);
	GameObjectType StringToGameObjectType(const char* gameObjectTypeStr);

	std::string ReplaceBackSlashesWithForward(std::string str);
	std::string RelativePathToAbsolute(const std::string& relativePath);

	std::string Replace(const std::string& str, const std::string& pattern, const std::string& replacement);

	// Returns random value in range [min, max)
	i32 RandomInt(i32 min, i32 max);

	// Returns random value in range [min, max)
	real RandomFloat(real min, real max);

	real MinComponent(const glm::vec2& vec);
	real MinComponent(const glm::vec3& vec);
	real MinComponent(const glm::vec4& vec);

	real MaxComponent(const glm::vec2& vec);
	real MaxComponent(const glm::vec3& vec);
	real MaxComponent(const glm::vec4& vec);

	inline real Saturate(real val)
	{
		return glm::clamp(val, 0.0f, 1.0f);
	}

	template<typename T>
	T Saturate(T val)
	{
		return glm::saturate(val);
	}

	glm::vec2 Floor(const glm::vec2& p);
	glm::vec2 Fract(const glm::vec2& p);

	glm::vec3 Floor(const glm::vec3& p);
	glm::vec3 Fract(const glm::vec3& p);

	u32 GenerateUID();

	template<typename T>
	bool Contains(const std::vector<T>& vec, T val)
	{
		return std::find(vec.begin(), vec.end(), val) != vec.end();
	}

	bool Contains(const std::vector<const char*>& vec, const char* val);

	bool Contains(const char* arr[], u32 arrLen, const char* val);

	template<class T>
	const T& PickRandomFrom(const std::vector<T>& vec)
	{
		return vec[RandomInt(0, (i32)vec.size())];
	}

	i32 RoundUp(i32 val, i32 alignment);

	// Returns true if value changed
	bool DoImGuiRotationDragFloat3(const char* label, glm::vec3& rotation, glm::vec3& outCleanedRotation);

	void CalculateOrthonormalBasis(const glm::vec3& n, glm::vec3& b1, glm::vec3& b2);

	enum class ImageFormat
	{
		JPG,
		TGA,
		PNG,
		BMP
	};

	bool SaveImage(const std::string& absoluteFilePath, ImageFormat imageFormat, i32 width, i32 height, i32 channelCount, u8* data, bool bFlipVertically = false);

	template<class T>
	inline typename std::vector<T>::const_iterator Find(const std::vector<T>& vec, const T& t)
	{
		for (typename std::vector<T>::const_iterator iter = vec.begin(); iter != vec.end(); ++iter)
		{
			if (*iter == t)
			{
				return iter;
			}
		}

		return vec.end();
	}

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
		glm::vec4 color;
		real xSpacing;
		real scale;

	private:
		//TextCache& operator=(const TextCache &tmp);

	};

	struct Vec2iCompare
	{
		bool operator()(const glm::vec2i& lhs, const glm::vec2i& rhs) const;
	};

	namespace ImGuiExt
	{
		bool InputUInt(const char* message, u32* v, u32 step = 1, u32 step_fast = 100, ImGuiInputTextFlags flags = 0);
		bool SliderUInt(const char* label, u32* v, u32 v_min, u32 v_max, const char* format = NULL);
		bool DragUInt(const char* label, u32* v, u32 v_min = 0, u32 v_max = 0, const char* format = "%d");
	} // namespace ImGuiExt
} // namespace flex
