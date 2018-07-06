#include "stdafx.hpp"

#include "Helpers.hpp"

#include <sstream>
#include <iomanip>
#include <fstream>

#pragma warning(push, 0)
#include <glm/gtx/matrix_decompose.hpp>

#include "AL/al.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#pragma warning(pop)

#include "FlexEngine.hpp"
#include "Logger.hpp"

namespace flex
{
	ImVec4 g_WarningTextColor(1.0f, 0.25f, 0.25f, 1.0f);
	ImVec4 g_WarningButtonColor(0.65f, 0.12f, 0.09f, 1.0f);
	ImVec4 g_WarningButtonHoveredColor(0.45f, 0.04f, 0.01f, 1.0f);
	ImVec4 g_WarningButtonActiveColor(0.35f, 0.0f, 0.0f, 1.0f);

	GLFWimage LoadGLFWimage(const std::string& filePath, bool alpha, bool flipVertically)
	{
		GLFWimage result = {};

		std::string fileName = filePath;
		StripLeadingDirectories(fileName);
		Logger::LogInfo("Loading texture " + fileName);

		stbi_set_flip_vertically_on_load(flipVertically);

		i32 channels;
		unsigned char* data = stbi_load(filePath.c_str(), &result.width, &result.height, &channels, alpha ? STBI_rgb_alpha : STBI_rgb);

		if (data == 0)
		{
			const char* failureReasonStr = stbi_failure_reason();
			Logger::LogError("Couldn't load image, failure reason: " + std::string(failureReasonStr) + " filepath: " + filePath);
			return result;
		}
		else
		{
			assert(result.width <= Renderer::MAX_TEXTURE_DIM);
			assert(result.height <= Renderer::MAX_TEXTURE_DIM);

			result.pixels = static_cast<unsigned char*>(data);
		}

		return result;
	}

	void DestroyGLFWimage(GLFWimage& image)
	{
		stbi_image_free(image.pixels);
		image.pixels = nullptr;
	}

	bool HDRImage::Load(const std::string& hdrFilePath, bool alpha, bool flipVertically)
	{
		filePath = hdrFilePath;

		std::string fileName = hdrFilePath;
		StripLeadingDirectories(fileName);
		Logger::LogInfo("Loading HDR texture " + fileName);

		stbi_set_flip_vertically_on_load(flipVertically);

		pixels = stbi_loadf(filePath.c_str(), &width, &height, &channelCount, alpha ? STBI_rgb_alpha : STBI_rgb);

		channelCount = 4;

		if (!pixels)
		{
			Logger::LogError("Failed to load HDR image at " + filePath);
			return false;
		}

		assert(width <= Renderer::MAX_TEXTURE_DIM);
		assert(height <= Renderer::MAX_TEXTURE_DIM);
		assert(channelCount > 0);

		return true;
	}

	void HDRImage::Free()
	{
		stbi_image_free(pixels);
	}

	std::string FloatToString(real f, i32 precision)
	{
		std::stringstream stream;

		stream << std::fixed << std::setprecision(precision) << f;

		return stream.str();
	}

	std::string IntToString(i32 i, u16 minChars)
	{
		std::string result = std::to_string(glm::abs(i));

		if (i < 0)
		{
			if (result.length() < minChars)
			{
				result = '-' + std::string(minChars - result.length(), '0') + result;
			}
			else
			{
				result = '-' + result;
			}
		}
		else
		{
			if (result.length() < minChars)
			{
				result = std::string(minChars - result.length(), '0') + result;
			}
		}

		return result;
	}

	bool FileExists(const std::string& filePath)
	{
		std::ifstream file(filePath.c_str());
		bool exists = file.good();
		file.close();

		return exists;
	}

	bool ReadFile(const std::string& filePath, std::string& fileContents, bool bBinaryFile)
	{
		int fileMode = std::ios::in | std::ios::ate;
		if (bBinaryFile)
		{
			fileMode |= std::ios::binary;
		}
		std::ifstream file(filePath.c_str(), fileMode);

		if (!file)
		{
			Logger::LogError("Unable to read file: " + filePath);
			return false;
		}

		std::streampos length = file.tellg();

		fileContents.resize((size_t)length);

		file.seekg(0, std::ios::beg);
		file.read(&fileContents[0], length);
		file.close();

		// Remove extra null terminators caused by Windows line endings
		for (u32 charIndex = 0; charIndex < fileContents.size() - 1; ++charIndex)
		{
			if (fileContents[charIndex] == '\0')
			{
				fileContents = fileContents.substr(0, charIndex);
			}
		}

		return true;
	}

	bool ReadFile(const std::string& filePath, std::vector<char>& vec, bool bBinaryFile)
	{
		i32 fileMode = std::ios::in | std::ios::ate;
		if (bBinaryFile)
		{
			fileMode |= std::ios::binary;
		}
		std::ifstream file(filePath.c_str(), fileMode);

		if (!file)
		{
			Logger::LogError("Unable to read file: " + filePath);
			return false;
		}

		std::streampos length = file.tellg();

		vec.resize((size_t)length);

		file.seekg(0, std::ios::beg);
		file.read(vec.data(), length);
		file.close();

		return true;
	}

	bool WriteFile(const std::string& filePath, const std::string& fileContents, bool bBinaryFile)
	{
		std::vector<char> vec(fileContents.begin(), fileContents.end());
		return WriteFile(filePath, vec, bBinaryFile);
	}

	bool WriteFile(const std::string& filePath, const std::vector<char>& vec, bool bBinaryFile)
	{
		i32 fileMode =std::ios::out | std::ios::trunc;
		if (bBinaryFile)
		{
			fileMode |= std::ios::binary;
		}
		std::ofstream fileStream(filePath, fileMode);

		if (fileStream.is_open())
		{
			fileStream.write(&vec[0], vec.size());
			fileStream.close();

			return true;
		}

		return false;
	}

	bool DeleteFile(const std::string& filePath, bool bPrintErrorOnFailure)
	{
		if (::DeleteFile(filePath.c_str()))
		{
			return true;
		}
		else
		{
			if (bPrintErrorOnFailure)
			{
				Logger::LogError("Failed to delete file " + filePath);
			}
			return false;
		}
	}

	bool CopyFile(const std::string& filePathFrom, const std::string& filePathTo)
	{
		if (::CopyFile(filePathFrom.c_str(), filePathTo.c_str(), 0))
		{
			return true;
		}
		else
		{
			Logger::LogError("Failed to copy file from \"" + filePathFrom + "\" to \"" + filePathTo + '\"');
			return false;
		}
	}

	bool DirectoryExists(const std::string& absoluteDirectoryPath)
	{
		if (absoluteDirectoryPath.find("..") != std::string::npos)
		{
			Logger::LogError("Attempted to create directory using relative path! Must specify absolute path!");
			return false;
		}

		DWORD dwAttrib = GetFileAttributes(absoluteDirectoryPath.c_str());

		return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
			    dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
	}

	bool FindFilesInDirectory(const std::string& directoryPath, std::vector<std::string>& filePaths, const std::string& fileType)
	{
		std::string cleanedFileType = fileType;
		{
			size_t dotPos = cleanedFileType.find('.');
			if (dotPos != std::string::npos)
			{
				cleanedFileType.erase(dotPos, 1);
			}
		}
		
		bool bPathContainsBackslash = (directoryPath.find('\\') != std::string::npos);
		char slashChar = (bPathContainsBackslash ? '\\' : '/');

		std::string cleanedDirPath = directoryPath;
		if (cleanedDirPath[cleanedDirPath.size() - 1] != slashChar)
		{
			cleanedDirPath += slashChar;
		}

		std::string cleanedDirPathWithWildCard = cleanedDirPath + '*';


		HANDLE hFind;
		WIN32_FIND_DATAA findData;

		hFind = FindFirstFile(cleanedDirPathWithWildCard.c_str(), &findData);
		
		if (hFind == INVALID_HANDLE_VALUE)
		{
			Logger::LogError("Failed to find any file in directory " + cleanedDirPath);
			return false;
		}

		do
		{
			if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				// Skip over directories
				//Logger::LogInfo(findData.cFileName);
			}
			else
			{
				bool foundFileTypeMatches = false;
				if (cleanedFileType == "*")
				{
					foundFileTypeMatches = true;
				}
				else
				{
					std::string fileNameStr(findData.cFileName);
					size_t dotPos = fileNameStr.find('.');

					if (dotPos != std::string::npos)
					{
						std::string foundFileType = Split(fileNameStr, '.')[1];
						if (foundFileType == cleanedFileType)
						{
							foundFileTypeMatches = true;
						}
					}
				}

				if (foundFileTypeMatches)
				{
					// File size retrieval:
					//LARGE_INTEGER filesize;
					//filesize.LowPart = findData.nFileSizeLow;
					//filesize.HighPart = findData.nFileSizeHigh;

					filePaths.push_back(cleanedDirPath + findData.cFileName);
				}
			}
		} while (FindNextFile(hFind, &findData) != 0);

		DWORD dwError = GetLastError();
		if (dwError != ERROR_NO_MORE_FILES)
		{
			Logger::LogError("Error encountered while finding files in directory " + cleanedDirPath);
			return false;
		}

		return true;
	}

	void StripLeadingDirectories(std::string& filePath)
	{
		size_t finalSlash = filePath.rfind('/');
		if (finalSlash == std::string::npos)
		{
			finalSlash = filePath.rfind('\\');
		}

		if (finalSlash == std::string::npos)
		{
			return; // There are no directories to remove
		}
		else
		{
			filePath = filePath.substr(finalSlash + 1);
		}
	}

	void ExtractDirectoryString(std::string& filePath)
	{
		size_t finalSlash = filePath.rfind('/');
		if (finalSlash == std::string::npos)
		{
			finalSlash = filePath.rfind('\\');
		}

		if (finalSlash == std::string::npos)
		{
			return; // There are no directories to remove
		}
		else
		{
			filePath = filePath.substr(0, finalSlash + 1);
		}
	}

	void StripFileType(std::string& filePath)
	{
		assert(filePath.find('.') != std::string::npos);

		filePath = Split(filePath, '.')[0];
	}

	void ExtractFileType(std::string& filePathInTypeOut)
	{
		assert(filePathInTypeOut.find('.') != std::string::npos);

		filePathInTypeOut = Split(filePathInTypeOut, '.')[1];
	}

	void CreateDirectoryRecursive(const std::string& absoluteDirectoryPath)
	{
		if (absoluteDirectoryPath.find("..") != std::string::npos)
		{
			Logger::LogError("Attempted to create directory using relative path! Must specify absolute path!");
			return;
		}

		if (DirectoryExists(absoluteDirectoryPath))
		{
			// Directory already exists!
			return;
		}

		u32 pos = 0;
		do 
		{
			pos = absoluteDirectoryPath.find_first_of("\\/", pos + 1);
			CreateDirectory(absoluteDirectoryPath.substr(0, pos).c_str(), NULL);
			GetLastError();// == ERROR_ALREADY_EXISTS;
		} while (pos != std::string::npos);
	}

	bool ParseWAVFile(const std::string& filePath, i32* format, u8** data, i32* size, i32* freq)
	{
		std::vector<char> dataArray;
		if (!ReadFile(filePath, dataArray, true))
		{
			Logger::LogError("Failed to parse WAV file: " + filePath);
			return false;
		}

		if (dataArray.size() < 12)
		{
			Logger::LogError("Invalid WAV file: " + filePath);
			return false;
		}

		u32 dataIndex = 0;
		std::string chunkID(&dataArray[dataIndex], 4); dataIndex += 4;

		u32 chunkSize = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		std::string waveID(&dataArray[dataIndex], 4); dataIndex += 4;
		std::string subChunk1ID(&dataArray[dataIndex], 4); dataIndex += 4;

		if (chunkID.compare("RIFF") != 0 ||
			waveID.compare("WAVE") != 0 ||
			subChunk1ID.compare("fmt ") != 0)
		{
			Logger::LogError("Invalid WAVE file header: " + filePath);
			return false;
		}

		// TODO: Unsigned?
		u32 subChunk1Size = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		if (subChunk1Size != 16)
		{
			Logger::LogError("Non-16 bit chunk size in WAVE files in unsupported: " + filePath);
			return false;
		}

		u16 audioFormat = Parse16u(&dataArray[dataIndex]); dataIndex += 2;
		if (audioFormat != 1) // WAVE_FORMAT_PCM
		{
			Logger::LogError("WAVE file uses unsupported format (only PCM is allowed): " + filePath);
			return false;
		}

		u16 channelCount = Parse16u(&dataArray[dataIndex]); dataIndex += 2;
		u32 samplesPerSec = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		u32 avgBytesPerSec = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		u16 blockAlign = Parse16u(&dataArray[dataIndex]); dataIndex += 2;
		u16 bitsPerSample = Parse16u(&dataArray[dataIndex]); dataIndex += 2;

		std::string subChunk2ID(&dataArray[dataIndex], 4); dataIndex += 4;
		if (subChunk2ID.compare("data") != 0)
		{
			Logger::LogError("Invalid WAVE file: " + filePath);
			return false;
		}

		u32 subChunk2Size = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		*data = new u8[subChunk2Size];
		for (u32 i = 0; i < subChunk2Size; ++i)
		{
			(*data)[i] = dataArray[dataIndex];
			++dataIndex;
		}

		bool bPrintWavStats = false;
		if (bPrintWavStats)
		{
			std::string fileName = filePath;
			StripLeadingDirectories(fileName);
			Logger::LogInfo("Stats about WAV file:" + fileName +
							":\nchannel count: " + std::to_string(channelCount) +
							", samples/s: " + std::to_string(samplesPerSec) +
							", average bytes/s: " + std::to_string(avgBytesPerSec) +
							", block align: " + std::to_string(blockAlign) +
							", bits/sample: " + std::to_string(bitsPerSample) +
							", chunk size: " + std::to_string(chunkSize) +
							", sub chunk2 ID: " + subChunk2ID +
							", sub chunk 2 size: " + std::to_string(subChunk2Size));
		}

		switch (channelCount)
		{
		case 1:
		{
			switch (bitsPerSample)
			{
			case 8:
				*format = AL_FORMAT_MONO8;
				break;
			case 16:
				*format = AL_FORMAT_MONO16;
				break;
			default:
				Logger::LogError("WAVE file contains invalid bitsPerSample (must be 8 or 16): " + std::to_string(bitsPerSample));
				break;
			}
		} break;
		case 2:
		{
			switch (bitsPerSample)
			{
			case 8:
				*format = AL_FORMAT_STEREO8;
				break;
			case 16:
				*format = AL_FORMAT_STEREO16;
				break;
			default:
				Logger::LogError("WAVE file contains invalid bitsPerSample (must be 8 or 16): " + std::to_string(bitsPerSample));
				break;
			}
		} break;
		default:
		{
			Logger::LogError("WAVE file contains invalid channel count (must be 1 or 2): " + std::to_string(channelCount));
		} break;
		}

		*size = subChunk2Size;
		*freq = samplesPerSec;

		return true;
	}

	u32 Parse32u(char* ptr)
	{
		return ((u8)ptr[0]) + ((u8)ptr[1] << 8) + ((u8)ptr[2] << 16) + ((u8)ptr[3] << 24);
	}

	u16 Parse16u(char* ptr)
	{
		return ((u8)ptr[0]) + ((u8)ptr[1] << 8);
	}

	std::vector<std::string> Split(const std::string& str, char delim)
	{
		std::vector<std::string> result;
		size_t i = 0;

		size_t strLen = str.size();
		while (i != strLen)
		{
			while (i != strLen && str[i] == delim)
			{
				++i;
			}

			size_t j = i;
			while (j != strLen && str[j] != delim)
			{
				++j;
			}

			if (i != j)
			{
				result.push_back(str.substr(i, j - i));
				i = j;
			}
		}

		return result;
	}

	i32 NextNonAlphaNumeric(const std::string& str, i32 offset)
	{
		while (offset < (i32)str.size())
		{
			if (!isdigit(str[offset]) && !isalpha(str[offset]))
			{
				return offset;
			}
			++offset;
		}

		return -1;
	}

	real Lerp(real a, real b, real t)
	{
		return a * (1.0f - t) + b * t;
	}

	glm::vec2 Lerp(const glm::vec2 & a, const glm::vec2 & b, real t)
	{
		return a * (1.0f - t) + b * t;
	}

	glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, real t)
	{
		return a * (1.0f - t) + b * t;
	}

	real ParseFloat(const std::string& floatStr)
	{
		if (floatStr.empty())
		{
			Logger::LogError("Invalid float string (empty)");
			return -1.0f;
		}

		return (real)std::atof(floatStr.c_str());
	}

	glm::vec2 ParseVec2(const std::string& vecStr)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if (parts.size() != 2)
		{
			Logger::LogError("Invalid vec2 field: " + vecStr);
			return glm::vec2(-1);
		}
		else
		{
			glm::vec2 result(
				std::atof(parts[0].c_str()),
				std::atof(parts[1].c_str()));

			return result;
		}
	}

	glm::vec3 ParseVec3(const std::string& vecStr)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if (parts.size() != 3 && parts.size() != 4)
		{
			Logger::LogError("Invalid vec3 field: " + vecStr);
			return glm::vec3(-1);
		}
		else
		{
			glm::vec3 result(
				std::atof(parts[0].c_str()),
				std::atof(parts[1].c_str()),
				std::atof(parts[2].c_str()));

			return result;
		}
	}

	glm::vec4 ParseVec4(const std::string& vecStr, real defaultW)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if ((parts.size() != 4 && parts.size() != 3) || (defaultW < 0 && parts.size() != 4))
		{
			Logger::LogError("Invalid vec4 field: " + vecStr);
			return glm::vec4(-1);
		}
		else
		{
			glm::vec4 result;

			if (parts.size() == 4)
			{
				result = glm::vec4(
					std::atof(parts[0].c_str()),
					std::atof(parts[1].c_str()),
					std::atof(parts[2].c_str()),
					std::atof(parts[3].c_str()));
			}
			else
			{
				result = glm::vec4(
					std::atof(parts[0].c_str()),
					std::atof(parts[1].c_str()),
					std::atof(parts[2].c_str()),
					defaultW);
			}

			return result;
		}
	}

	bool IsNanOrInf(const glm::vec2& vec)
	{
		return (isnan(vec.x) || isnan(vec.y) || 
				isinf(vec.x) || isinf(vec.y));
	}

	bool IsNanOrInf(const glm::vec3& vec)
	{
		return (isnan(vec.x) || isnan(vec.y) || isnan(vec.z) ||
				isinf(vec.x) || isinf(vec.y) || isinf(vec.z));
	}

	bool IsNanOrInf(const glm::vec4& vec)
	{
		return (isnan(vec.x) || isnan(vec.y) || isnan(vec.z) || isnan(vec.w) ||
				isinf(vec.x) || isinf(vec.y) || isinf(vec.z) || isinf(vec.w));
	}

	bool IsNanOrInf(const glm::quat& quat)
	{
		return (isnan(quat.x) || isnan(quat.y) || isnan(quat.z) || isnan(quat.w) ||
				isinf(quat.x) || isinf(quat.y) || isinf(quat.z) || isinf(quat.w));
	}

	std::string Vec2ToString(const glm::vec2& vec)
	{
		std::string result(std::to_string(vec.x) + ", " +
			std::to_string(vec.y));
		return result;
	}

	std::string Vec3ToString(const glm::vec3& vec)
	{
		std::string result(std::to_string(vec.x) + ", " + 
			std::to_string(vec.y) + ", " + 
			std::to_string(vec.z));
		return result;
	}

	std::string Vec4ToString(const glm::vec4& vec)
	{
		std::string result(std::to_string(vec.x) + ", " +
			std::to_string(vec.y) + ", " + 
			std::to_string(vec.z) + ", " + 
			std::to_string(vec.w));
		return result;
	}

	void CopyColorToClipboard(const glm::vec3& col)
	{
		CopyColorToClipboard({ col.x, col.y, col.z, 1.0f });
	}

	void CopyColorToClipboard(const glm::vec4& col)
	{
		ImGui::LogToClipboard();

		ImGui::LogText("%.2ff,%.2ff,%.2ff,%.2ff", col.x, col.y, col.z, col.w);

		ImGui::LogFinish();
	}

	glm::vec3 PasteColor3FromClipboard()
	{
		glm::vec4 color4 = PasteColor4FromClipboard();

		return glm::vec3(color4);
	}

	glm::vec4 PasteColor4FromClipboard()
	{
		const std::string clipboardContents = ImGui::GetClipboardText();

		const size_t comma1 = clipboardContents.find(',');
		const size_t comma2 = clipboardContents.find(',', comma1 + 1);
		const size_t comma3 = clipboardContents.find(',', comma2 + 1);

		if (comma1 == std::string::npos ||
			comma2 == std::string::npos ||
			comma3 == std::string::npos)
		{
			// Clipboard doesn't contain correctly formatted color!
			return glm::vec4(0.0f);
		}

		glm::vec4 result(
			stof(clipboardContents.substr(0, comma1)),
			stof(clipboardContents.substr(comma1 + 1, comma2 - comma1 - 1)),
			stof(clipboardContents.substr(comma2 + 1, comma3 - comma2 - 1)),
			stof(clipboardContents.substr(comma3 + 1 ))
		);

		return result;
	}

	CullFace StringToCullFace(const std::string& str)
	{
		std::string strLower(str);
		ToLower(strLower);

		if (strLower.compare("back") == 0)
		{
			return CullFace::BACK;
		}
		else if (strLower.compare("front") == 0)
		{
			return CullFace::FRONT;
		}
		else if (strLower.compare("front and back") == 0)
		{
			return CullFace::FRONT_AND_BACK;
		}
		else
		{
			Logger::LogError("Unhandled cull face str: " + str);
			return CullFace::NONE;
		}
	}

	std::string CullFaceToString(CullFace cullFace)
	{
		switch (cullFace)
		{
		case CullFace::BACK:			return "back";
		case CullFace::FRONT:			return "front";
		case CullFace::FRONT_AND_BACK:	return "front and back";
		case CullFace::NONE:			return "NONE";
		default:						return "UNHANDLED CULL FACE";
		}
	}

	void ToLower(std::string& str)
	{
		for (char& c : str)
		{
			c = (char)tolower(c);
		}
	}

	void ToUpper(std::string& str)
	{
		for (char& c : str)
		{
			c = (char)toupper(c);
		}
	}

	bool StartsWith(const std::string& str, const std::string& start)
	{
		if (str.length() < start.length())
		{
			return false;
		}

		bool result = (str.substr(0, start.length()).compare(start) == 0);
		return result;
	}

	bool EndsWith(const std::string& str, const std::string& end)
	{
		if (str.length() < end.length())
		{
			return false;
		}

		bool result = (str.substr(str.length() - end.length()).compare(end) == 0);
		return result;
	}

	i32 GetNumberEndingWith(const std::string& str, i16& outNumNumericalChars)
	{
		if (str.empty())
		{
			outNumNumericalChars = 0;
			return -1;
		}

		u16 strLen = (u16)str.size();

		if (!isdigit(str[strLen - 1]))
		{
			outNumNumericalChars = 0;
			return -1;
		}

		i16 firstDigit = strLen - 1;
		while (firstDigit >= 0 && isdigit(str[firstDigit]))
		{
			firstDigit--;
		}
		firstDigit++;

		i32 num = (i32)atoi(str.substr(firstDigit).c_str());
		outNumNumericalChars = (strLen - firstDigit);

		return num;
	}

	std::string GameObjectTypeToString(GameObjectType type)
	{
		return GameObjectTypeStrings[(i32)type];
	}

	GameObjectType StringToGameObjectType(const std::string& gameObjectTypeStr)
	{
		for (i32 i = 0; i < (i32)GameObjectType::NONE; ++i)
		{
			if (GameObjectTypeStrings[i].compare(gameObjectTypeStr) == 0)
			{
				return (GameObjectType)i;
			}
		}

		return GameObjectType::NONE;
	}

	void RetrieveCurrentWorkingDirectory()
	{
		char cwdBuffer[MAX_PATH];
		char* ans = _getcwd(cwdBuffer, sizeof(cwdBuffer));
		if (ans)
		{
			FlexEngine::s_CurrentWorkingDirectory = ans;
		}
	}

	std::string RelativePathToAbsolute(const std::string& relativePath)
	{
		size_t nextDoubleDot = relativePath.find("..");

		std::string workingDirectory = FlexEngine::s_CurrentWorkingDirectory;

		std::string strippedFilePath = relativePath;

		while (nextDoubleDot != std::string::npos)
		{
			size_t lastSlash = workingDirectory.find_last_of("\\/");
			if (lastSlash == std::string::npos)
			{
				Logger::LogWarning("Invalidly formed relative path! " + relativePath);
				nextDoubleDot = std::string::npos;
			}
			else
			{
				workingDirectory = workingDirectory.substr(0, lastSlash);
				strippedFilePath = strippedFilePath.substr(nextDoubleDot);
				nextDoubleDot = relativePath.find("..", nextDoubleDot + 2);
			}
		}

		std::for_each(strippedFilePath.begin(), strippedFilePath.end(), [](char& c) 
		{
			if (c == '/')
			{
				c = '\\';
			}
		});

		std::string absolutePath = workingDirectory + '\\' + strippedFilePath;

		return absolutePath;
	}
} // namespace flex
