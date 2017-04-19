#pragma once

#include "Logger.h"

#include <vector>
#include <fstream>

std::vector<uint8_t> ReadFile(const std::string& filePath);
