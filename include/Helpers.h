#pragma once

#include <string>
#include <vector>

std::string FloatToString(float f, int precision);

std::vector<char> ReadFile(const std::string& filePath);
