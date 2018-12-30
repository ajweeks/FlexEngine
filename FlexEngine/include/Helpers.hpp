#pragma once

#include <direct.h> // For _getcwd
#include <vector>

#pragma warning(push, 0)
#include <assimp/color4.h>
#include <assimp/vector2.h>
#include <assimp/vector3.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#if COMPILE_IMGUI
#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#include "imgui.h"
#endif

#pragma warning(pop)

#include "Graphics/RendererTypes.hpp"

namespace flex
{
	extern ImVec4 g_WarningTextColor;
	extern ImVec4 g_WarningButtonColor;
	extern ImVec4 g_WarningButtonHoveredColor;
	extern ImVec4 g_WarningButtonActiveColor;

	static const char* SEPARATOR_STR = ", ";

	GLFWimage LoadGLFWimage(const std::string& filePath, i32 requestedChannelCount = 3, bool flipVertically = false, i32* channelCountOut = nullptr);
	void DestroyGLFWimage(GLFWimage& image);

	bool FileExists(const std::string& filePath);

	bool ReadFile(const std::string& filePath, std::string& fileContents, bool bBinaryFile);
	bool ReadFile(const std::string& filePath, std::vector<char>& vec, bool bBinaryFile);

	bool WriteFile(const std::string& filePath, const std::string& fileContents, bool bBinaryFile);
	bool WriteFile(const std::string& filePath, const std::vector<char>& vec, bool bBinaryFile);

	bool DeleteFile(const std::string& filePath, bool bPrintErrorOnFailure = true);

	bool CopyFile(const std::string& filePathFrom, const std::string& filePathTo);

	bool DirectoryExists(const std::string& absoluteDirectoryPath);

	void OpenExplorer(const std::string& absoluteDirectory);
	bool OpenJSONFileDialog(const std::string& windowTitle, const std::string& absoluteDirectory, std::string& outSelectedAbsFilePath);
	bool OpenFileDialog(const std::string& windowTitle, const std::string& absoluteDirectory, std::string& outSelectedAbsFilePath, char filter[] = nullptr);

	// Returns true if any files were found
	// Set fileType to "*" to retrieve all files
	bool FindFilesInDirectory(const std::string& directoryPath, std::vector<std::string>& filePaths, const std::string& fileType);

	// Removes all content before final '/' or '\'
	void StripLeadingDirectories(std::string& filePath);

	// Removes all content after final '/' or '\'
	// NOTE: If path describes a directory and doesn't end in a slash, final directory will be removed
	void ExtractDirectoryString(std::string& filePath);

	// Removes all chars after first '.' occurrence
	void StripFileType(std::string& filePath);

	// Removes all chars before first '.' occurrence
	void ExtractFileType(std::string& filePathInTypeOut);

	// Creates directories for each listed in string
	void CreateDirectoryRecursive(const std::string& absoluteDirectoryPath);

	/*
	* Reads in a .wav file and fills in given values according to file contents
	* Returns true if reading and parsing succeeded
	*/
	bool ParseWAVFile(const std::string& filePath, i32* format, u8** data, i32* size, i32* freq);

	/* Interpret 4 bytes starting at ptr as an unsigned 32-bit int */
	u32 Parse32u(char* ptr);
	/* Interpret 2 bytes starting at ptr as an unsigned 16-bit int */
	u16 Parse16u(char* ptr);

	// Returns the current year, month, & day  (YYYY-MM-DD)
	std::string GetDateString_YMD();
	// Returns the current year, month, day, hour, minute, & second (YYYY-MM-DD_HH-MM-SS)
	std::string GetDateString_YMDHMS();

	std::vector<std::string> Split(const std::string& str, char delim);

	/*
	 * Returns the index of the first character which isn't a number
	 * of letter (or -1 if none exist) starting from offset
	 */
	i32 NextNonAlphaNumeric(const std::string& str, i32 offset);

	bool NearlyEquals(real a, real b, real threshhold);
	bool NearlyEquals(const glm::vec2& a, const glm::vec2& b, real threshhold);
	bool NearlyEquals(const glm::vec3& a, const glm::vec3& b, real threshhold);
	bool NearlyEquals(const glm::vec4& a, const glm::vec4& b, real threshhold);

	glm::vec3 MoveTowards(const glm::vec3& a, const glm::vec3& b, real delta);
	real MoveTowards(const real& a, real b, real delta);

	real Lerp(real a, real b, real t);
	glm::vec2 Lerp(const glm::vec2& a, const glm::vec2& b, real t);
	glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, real t);
	glm::vec4 Lerp(const glm::vec4& a, const glm::vec4& b, real t);

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

	bool IsNanOrInf(real val);
	bool IsNanOrInf(const glm::vec2& vec);
	bool IsNanOrInf(const glm::vec3& vec);
	bool IsNanOrInf(const glm::vec4& vec);
	bool IsNanOrInf(const glm::quat& quat);

	std::string GetIncrementedPostFixedStr(const std::string& namePrefix, const std::string& defaultName);

	void PadEnd(std::string& str, i32 minLen, char pad);
	void PadStart(std::string& str, i32 minLen, char pad);

	std::string FloatToString(real f, i32 precision);

	// String will be padded with '0's to be at least minChars long (excluding a leading '-' for negative numbers)
	std::string IntToString(i32 i, u16 minChars = 0);

	std::string Vec2ToString(glm::vec2 vec, i32 precision);
	std::string Vec3ToString(glm::vec3 vec, i32 precision);
	std::string Vec4ToString(glm::vec4 vec, i32 precision);

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

	void ToLower(std::string& str);
	void ToUpper(std::string& str);

	bool StartsWith(const std::string& str, const std::string& start);
	bool EndsWith(const std::string& str, const std::string& end);

	// Returns the number str ends with or -1 if last char isn't numeral
	// outNumNumericalChars will be set to the number of chars in the num (e.g. "001" => 3)
	i32 GetNumberEndingWith(const std::string& str, i16& outNumNumericalChars);

	const char* GameObjectTypeToString(GameObjectType type);
	GameObjectType StringToGameObjectType(const char* gameObjectTypeStr);

	// Must be called at least once to set g_CurrentWorkingDirectory!
	void RetrieveCurrentWorkingDirectory();
	std::string RelativePathToAbsolute(const std::string& relativePath);

	// Returns random value in range [min, max)
	i32 RandomInt(i32 min, i32 max);

	// Returns true if value changed
	bool DoImGuiRotationDragFloat3(const char* label, glm::vec3& rotation, glm::vec3& outCleanedRotation);

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
		for (std::vector<T>::const_iterator iter = vec.begin(); iter != vec.end(); ++iter)
		{
			if (*iter == t)
			{
				return iter;
			}
		}

		return vec.end();
	}

	template<class TReal>
	glm::vec2 ToVec2(const aiVector2t<TReal>& vec)
	{
		return glm::vec2(vec.x, vec.y);
	}

	template<class TReal>
	glm::vec3 ToVec3(const aiVector3t<TReal>& vec)
	{
		return glm::vec3(vec.x, vec.y, vec.z);
	}

	template<class TReal>
	glm::vec4 ToVec4(const aiColor4t<TReal>& color)
	{
		return glm::vec4(color.r, color.g, color.b, color.a);
	}

	struct HDRImage
	{
		bool Load(const std::string& hdrFilePath, i32 requestedChannelCount, bool flipVertically);
		void Free();

		i32 width;
		i32 height;
		i32 channelCount;
		std::string filePath;
		real* pixels;
	};

	// TODO: Move enums to their own header
	enum class SamplingType
	{
		CONSTANT, // All samples are equally-weighted
		LINEAR    // Latest sample is weighted N times higher than Nth sample
	};

	enum class TurningDir
	{
		LEFT,
		NONE,
		RIGHT
	};

	enum class TransformState
	{
		TRANSLATE,
		ROTATE,
		SCALE,
		NONE
	};

	enum class TrackState
	{
		FACING_FORWARD,
		FACING_BACKWARD,
		// NOTE: Add all elements to TrackStateStrs in Helpers.cpp
		NONE
	};

	extern const char* TrackStateStrs[((i32)TrackState::NONE) + 1];

	const char* TrackStateToString(TrackState state);

	enum class LookDirection
	{
		LEFT,
		CENTER,
		RIGHT,

		NONE
	};

	template <class T>
	struct RollingAverage
	{
		RollingAverage();
		RollingAverage(i32 valueCount, SamplingType samplingType = SamplingType::CONSTANT);

		void AddValue(T newValue);
		void Reset();
		void Reset(const T& resetValue);

		T currentAverage;
		std::vector<T> prevValues;

		SamplingType samplingType;

		i32 currentIndex = 0;
	};

	template<class T>
	inline RollingAverage<T>::RollingAverage()
	{
	}

	template <class T>
	RollingAverage<T>::RollingAverage(i32 valueCount, SamplingType samplingType /* = SamplingType::CONSTANT */) :
		samplingType(samplingType)
	{
		prevValues.resize(valueCount);
	}

	template <class T>
	void RollingAverage<T>::AddValue(T newValue)
	{
		prevValues[currentIndex++] = newValue;
		currentIndex %= prevValues.size();

		currentAverage = T();
		i32 sampleCount = 0;
		i32 valueCount = (i32)prevValues.size();
		for (i32 i = 0; i < valueCount; ++i)
		{
			switch (samplingType)
			{
			case SamplingType::CONSTANT:
				currentAverage += prevValues[i];
				sampleCount++;
				break;
			case SamplingType::LINEAR:
				real sampleWeight = (real)(i <= currentIndex ? i + (valueCount - currentIndex) : i - currentIndex);
				currentAverage += prevValues[i] * sampleWeight;
				sampleCount += (i32)sampleWeight;
				break;
			}
		}

		if (sampleCount > 0)
		{
			currentAverage /= sampleCount;
		}
	}

	template <class T>
	void RollingAverage<T>::Reset()
	{
		for (T& v : prevValues)
		{
			v = T();
		}
		currentIndex = 0;
		currentAverage = T();
	}

	template<class T>
	inline void RollingAverage<T>::Reset(const T& resetValue)
	{
		for (T& v : prevValues)
		{
			v = resetValue;
		}
		currentIndex = 0;
		currentAverage = resetValue;
	}

	// Stores text render commands issued during the
	// frame to later be converted to "TextVertex"s
	struct TextCache
	{
	public:
		TextCache(const std::string& text, AnchorPoint anchor, glm::vec2 position, glm::vec4 col, real xSpacing, bool bRaw, const std::vector<glm::vec2>& letterOffsets);

		std::string str;
		AnchorPoint anchor;
		glm::vec2 pos;
		glm::vec4 color;
		real xSpacing;
		bool bRaw;
		std::vector<glm::vec2> letterOffsets;

	private:
		//TextCache& operator=(const TextCache &tmp);

	};
} // namespace flex
