#pragma once

#include <string>
#include <vector>

std::string FloatToString(float f, int precision);

std::vector<char> ReadFile(const std::string& filePath);

glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, float t);
