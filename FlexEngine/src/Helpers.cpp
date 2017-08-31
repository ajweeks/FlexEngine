#include "stdafx.h"

#include "Helpers.h"

#include <sstream>
#include <iomanip>
#include <fstream>

#include <imgui.h>

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

	void ToString(const glm::vec2& vec, std::ostream& stream)
	{
		stream << vec.x << " " << vec.y;
	}

	void ToString(const glm::vec3& vec, std::ostream& stream)
	{
		stream << vec.x << " " << vec.y << " " << vec.z;
	}

	void ToString(const glm::vec4& vec, std::ostream& stream)
	{
		stream << vec.x << " " << vec.y << " " << vec.z << " " << vec.w;
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
	
} // namespace flex
