#pragma once

#include <string>
#include <vector>

#include <glm\vec2.hpp>
#include <glm\vec3.hpp>
#include <glm\vec4.hpp>

#include <assimp\vector2.h>
#include <assimp\vector3.h>
#include <assimp\color4.h>

std::string FloatToString(float f, int precision);

std::vector<char> ReadFile(const std::string& filePath);

glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, float t);

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
