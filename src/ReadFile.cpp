
#include "stdafx.h"

#include "ReadFile.h"

std::vector<uint8_t> ReadFile(const std::string& filePath)
{
	std::ifstream inFile(filePath, std::ios::binary, std::ios::ate);

	if (!inFile)
	{
		Logger::LogError("ReadFile couldn't find specified file: " + filePath);
		return{};
	}

	std::streampos fileLength = inFile.tellg();

	std::vector<uint8_t> data;
	data.resize((size_t)fileLength);

	inFile.seekg(0, std::ios::beg);

	inFile.read(reinterpret_cast<char*>(data.data()), fileLength);

	inFile.close();

	return data;
}