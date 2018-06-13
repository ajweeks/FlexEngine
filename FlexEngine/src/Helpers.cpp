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

	std::string FloatToString(real f, i32 precision)
	{
		std::stringstream stream;

		stream << std::fixed << std::setprecision(precision) << f;

		return stream.str();
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
		std::string directoryStr = filePath;
		ExtractDirectoryString(directoryStr);

		CreateDirectoryRecursive(directoryStr);

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

	std::string GameObjectTypeToString(GameObjectType type)
	{
		switch (type)
		{
		case GameObjectType::OBJECT:				return "object";
		case GameObjectType::PLAYER:				return "player";
		case GameObjectType::SKYBOX:				return "skybox";
		case GameObjectType::REFLECTION_PROBE:		return "reflection probe";
		case GameObjectType::VALVE:					return "valve";
		case GameObjectType::RISING_BLOCK:			return "rising block";
		case GameObjectType::GLASS_WINDOW:			return "glass window";
		case GameObjectType::NONE:					return "NONE";
		default:									return "UNHANDLED GAME OBJECT TYPE";
		}
	}

	GameObjectType StringToGameObjectType(const std::string& gameObjectTypeStr)
	{
		if (gameObjectTypeStr.compare("object") == 0)
		{
			return GameObjectType::OBJECT;
		}
		if (gameObjectTypeStr.compare("player") == 0)
		{
			return GameObjectType::PLAYER;
		}
		else if (gameObjectTypeStr.compare("skybox") == 0)
		{
			return GameObjectType::SKYBOX;
		}
		else if (gameObjectTypeStr.compare("reflection probe") == 0)
		{
			return GameObjectType::REFLECTION_PROBE;
		}
		else if (gameObjectTypeStr.compare("valve") == 0)
		{
			return GameObjectType::VALVE;
		}
		else if (gameObjectTypeStr.compare("rising block") == 0)
		{
			return GameObjectType::RISING_BLOCK;
		}
		else if (gameObjectTypeStr.compare("glass window") == 0)
		{
			return GameObjectType::GLASS_WINDOW;
		}
		else
		{
			return GameObjectType::NONE;
		}
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

	bool HDRImage::Load(const std::string& hdrFilePath, bool flipVertically)
	{
		filePath = hdrFilePath;

		std::string fileName = hdrFilePath;
		StripLeadingDirectories(fileName);
		Logger::LogInfo("Loading HDR texture " + fileName);

		stbi_set_flip_vertically_on_load(flipVertically);

		pixels = stbi_loadf(filePath.c_str(), &width, &height, &channelCount, STBI_rgb_alpha);

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

	RollingAverage::RollingAverage() :
		RollingAverage(8)
	{
	}

	RollingAverage::RollingAverage(i32 valueCount)
	{
		prevValues.resize(valueCount);
	}

	void RollingAverage::AddValue(real newValue)
	{
		prevValues[currentIndex++] = newValue;
		currentIndex %= prevValues.size();

		currentAverage = 0.0f;
		std::for_each(prevValues.begin(), prevValues.end(), [this](real value)
		{
			currentAverage += value;
		});

		currentAverage /= prevValues.size();
	}

	void RollingAverage::Reset()
	{
		for (real& v : prevValues)
		{
			v = 0.0f;
		}
		currentIndex = 0;
		currentAverage = 0.0f;
	}

} // namespace flex
