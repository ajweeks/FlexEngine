#include "stdafx.h"

#include "Helpers.h"

#include <sstream>
#include <iomanip>
#include <fstream>

#include "Logger.h"

namespace flex
{
	std::string FloatToString(float f, int precision)
	{
		std::stringstream stream;

		stream << std::fixed << std::setprecision(precision) << f;

		return stream.str();
	}

	std::vector<char> ReadFile(const std::string& filePath)
	{
		std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary | std::ios::ate);

		if (!file)
		{
			Logger::LogError("Unable to read file " + filePath);
			return{};
		}

		std::streampos length = file.tellg();

		std::vector<char> chars;
		chars.resize((size_t)length);

		file.seekg(0, std::ios::beg);
		file.read(chars.data(), length);
		file.close();

		return chars;
	}

	glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, float t)
	{
		return a * (1.0f - t) + b * t;
	}
} // namespace flex
