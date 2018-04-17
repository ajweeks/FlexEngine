#pragma once

#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <assimp/vector2.h>
#include <assimp/vector3.h>
#include <assimp/color4.h>

#include "imgui.h"


namespace flex
{
	GLFWimage LoadGLFWimage(const std::string& filePath, bool alpha = false, bool flipVertically = false);
	void DestroyGLFWimage(GLFWimage& image);

	struct HDRImage
	{
		bool Load(const std::string& hdrFilePath, bool flipVertically);
		void Free();

		i32 width;
		i32 height;
		i32 channelCount;
		std::string filePath;
		real* pixels;
	};

	std::string FloatToString(real f, i32 precision);

	bool ReadFile(const std::string& filePath, std::vector<char>& vec);

	// Removes all content before the final '/' or '\' 
	void StripLeadingDirectories(std::string& filePath);

	/*
	 * Returns the index of the first character which isn't a number
	 * of letter (or -1 if none exist) starting from offset
	 */
	i32 NextNonAlphaNumeric(const std::string& str, i32 offset);

	real Lerp(real a, real b, real t);
	glm::vec2 Lerp(const glm::vec2& a, const glm::vec2& b, real t);
	glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, real t);

	template<class T>
	inline typename T::const_iterator Contains(const std::vector<T>& vec, const T& t)
	{
		for (auto iter = vec.begin(); iter != vec.end(); ++iter)
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

	void ToString(const glm::vec2& vec, std::ostream& stream);
	void ToString(const glm::vec3& vec, std::ostream& stream);
	void ToString(const glm::vec4& vec, std::ostream& stream);

	void CopyColorToClipboard(const glm::vec4& col);
	void CopyColorToClipboard(const glm::vec3& col);

	glm::vec3 PasteColor3FromClipboard();
	glm::vec4 PasteColor4FromClipboard();

} // namespace flex
