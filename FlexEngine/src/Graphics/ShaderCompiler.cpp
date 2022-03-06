#include "stdafx.hpp"

#if COMPILE_SHADER_COMPILER

#include "Graphics/ShaderCompiler.hpp"

#include "FlexEngine.hpp"
#include "Platform/Platform.hpp"
#include "Time.hpp"

namespace flex
{
	std::string ShaderCompiler::s_ChecksumFilePathAbs;

	const char* ShaderCompiler::s_RecognizedShaderTypes[] = { "vert", "geom", "frag", "comp", "glsl" };

	volatile u32 shaderWorkItemsCreated = 0;
	volatile u32 shaderWorkItemsClaimed = 0;
	volatile u32 shaderWorkItemsCompleted = 0;
	ShaderCompiler::ShaderCompilationResult** s_ShaderCompilationResults;
	u32 s_ShaderCompilationResultsLength = 0;
	std::vector<ShaderCompiler::ShaderError> s_ShaderErrors;

	ShaderCompiler::ShaderCompiler()
	{
		s_ChecksumFilePathAbs = RelativePathToAbsolute(SHADER_CHECKSUM_LOCATION);

		const std::string spvDirectoryAbs = RelativePathToAbsolute(COMPILED_SHADERS_DIRECTORY);
		if (!Platform::DirectoryExists(spvDirectoryAbs))
		{
			Platform::CreateDirectoryRecursive(spvDirectoryAbs);
		}
	}

	ShaderCompiler::~ShaderCompiler()
	{
		if (s_ShaderCompilationResults != nullptr)
		{
			for (u32 i = 0; i < s_ShaderCompilationResultsLength; ++i)
			{
				delete s_ShaderCompilationResults[i];
			}

			delete[] s_ShaderCompilationResults;
			s_ShaderCompilationResults = nullptr;

			s_ShaderCompilationResultsLength = 0;
		}
	}

	void LoadShaderAsync(ShaderThreadData* threadData)
	{
		while (threadData->bRunning)
		{
			ShaderCompiler::ShaderCompilationResult* compilationResult = nullptr;

			// Check for new unclaimed work items
			if (shaderWorkItemsCreated != shaderWorkItemsClaimed)
			{
				Platform::EnterCriticalSection(threadData->criticalSection);
				if (shaderWorkItemsCreated != shaderWorkItemsClaimed)
				{
					compilationResult = s_ShaderCompilationResults[shaderWorkItemsClaimed];
					shaderWorkItemsClaimed += 1;

					WRITE_BARRIER;

				}
				Platform::LeaveCriticalSection(threadData->criticalSection);
			}

			// Complete work item if claimed
			if (compilationResult != nullptr)
			{
				shaderc::Compiler compiler;
				if (compiler.IsValid())
				{
					std::string shaderAbsPath = compilationResult->shaderAbsPath;
					std::string fileContents;
					if (ReadFile(shaderAbsPath, fileContents, false))
					{
						shaderc::CompileOptions options = {};
						options.SetOptimizationLevel(shaderc_optimization_level_performance);
						// TODO: Remove for release builds
						options.SetGenerateDebugInfo();
						options.SetWarningsAsErrors();

						options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
						options.SetTargetSpirv(shaderc_spirv_version_1_5);

						options.SetIncluder(std::make_unique<FlexShaderIncluder>());

						compilationResult->bSuccess = true;

						std::string fileType = ExtractFileType(shaderAbsPath);
						shaderc_shader_kind shaderKind = FilePathToShaderKind(fileType.c_str());

						if (threadData->bGenerateAssembly)
						{
							compilationResult->assemblyResult = compiler.CompileGlslToSpvAssembly(fileContents, shaderKind, shaderAbsPath.c_str(), options);
							compilationResult->bSuccess = compilationResult->assemblyResult.GetCompilationStatus() == shaderc_compilation_status_success;
						}

						if (compilationResult->bSuccess)
						{
							compilationResult->spvResult = compiler.CompileGlslToSpv(fileContents, shaderKind, shaderAbsPath.c_str(), options);
							compilationResult->bSuccess = compilationResult->spvResult.GetCompilationStatus() == shaderc_compilation_status_success;
						}
					}
					else
					{
						compilationResult->errorStr = "Failed to read shader file from " + shaderAbsPath;
						compilationResult->bSuccess = false;
					}
				}
				else
				{
					compilationResult->errorStr = "Unable to compile shaders, compiler is not valid!\n";
					compilationResult->bSuccess = false;
				}

				WRITE_BARRIER;

				Platform::AtomicIncrement(&shaderWorkItemsCompleted);
			}
			else
			{
				Platform::Sleep(1);
			}
		}
	}

	void ShaderCompiler::QueueWorkItem(const std::string& shaderAbsPath)
	{
		CHECK_LT(shaderWorkItemsCreated, s_ShaderCompilationResultsLength);

		s_ShaderCompilationResults[shaderWorkItemsCreated] = new ShaderCompilationResult();
		s_ShaderCompilationResults[shaderWorkItemsCreated]->shaderAbsPath = shaderAbsPath;

		Platform::AtomicIncrement(&shaderWorkItemsCreated);
	}

	//void ShaderCompiler::bComplete()
	//{
	//	Print("Compiled %s\n", fileName.c_str());
	//}

	void ShaderCompiler::ClearShaderHash(const std::string& shaderName)
	{
		if (FileExists(SHADER_SOURCE_DIRECTORY))
		{
			std::string fileContents;
			if (ReadFile(SHADER_SOURCE_DIRECTORY, fileContents, false))
			{
				std::string searchStr = "vk_" + shaderName + '.';
				size_t index = 0;
				do
				{
					index = fileContents.find(searchStr, index);
					if (index != std::string::npos)
					{
						size_t prevNewLine = fileContents.rfind('\n', index);
						size_t nextNewLine = fileContents.find('\n', index);
						if (prevNewLine == std::string::npos)
						{
							prevNewLine = 0;
						}
						if (nextNewLine == std::string::npos)
						{
							nextNewLine = fileContents.size() - 1;
						}
						fileContents = fileContents.substr(0, prevNewLine + 1) + fileContents.substr(nextNewLine + 1);
					}
				} while (index != std::string::npos);

				WriteFile(s_ChecksumFilePathAbs, fileContents, false);
			}
		}
	}

	void ShaderCompiler::DrawImGuiShaderErrorsWindow(bool* bWindowShowing)
	{
		if (!s_ShaderErrors.empty())
		{
			if (bWindowShowing == nullptr || *bWindowShowing)
			{
				ImGui::SetNextWindowSize(ImVec2(800.0f, 150.0f), ImGuiCond_Appearing);
				if (ImGui::Begin("Shader errors", bWindowShowing))
				{
					DrawImGuiShaderErrors();
				}

				ImGui::End();
			}
		}
	}

	void ShaderCompiler::DrawImGuiShaderErrors()
	{
		if (!s_ShaderErrors.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
			ImGui::Text(s_ShaderErrors.size() > 1 ? "%u errors" : "%u error", (u32)s_ShaderErrors.size());
			ImGui::PopStyleColor();

			ImGui::Separator();

			for (const ShaderError& shaderError : s_ShaderErrors)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
				ImGui::TextWrapped("%s", shaderError.errorStr.c_str());
				ImGui::PopStyleColor();

				std::string fileName = StripLeadingDirectories(shaderError.filePath);
				std::string openStr = "Open " + fileName;
				if (shaderError.lineNumber != -1)
				{
					openStr += ":" + std::to_string(shaderError.lineNumber);
				}
				if (ImGui::Button(openStr.c_str()))
				{
					std::string shaderEditorPath = g_EngineInstance->GetShaderEditorPath();
					if (shaderEditorPath.empty())
					{
						Platform::OpenFileWithDefaultApplication(shaderError.filePath);
					}
					else
					{
						std::string param0 = shaderError.filePath;
						if (shaderError.lineNumber != -1)
						{
							param0 += ":" + std::to_string(shaderError.lineNumber);
						}
						Platform::LaunchApplication(shaderEditorPath.c_str(), param0);
					}
				}

				ImGui::Separator();
			}
		}
	}

	void ShaderCompiler::StartCompilation(bool bForceCompileAll /* = false */)
	{
		m_ThreadData.bRunning = false;

		WRITE_BARRIER;

		for (std::thread& thread : m_Threads)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}

		m_Threads.clear();
		m_QueuedLoads.clear();

		if (m_ThreadData.criticalSection != nullptr)
		{
			Platform::FreeCriticalSection(m_ThreadData.criticalSection);
			m_ThreadData.criticalSection = nullptr;
		}
		bComplete = true;
		shaderWorkItemsCreated = 0;
		shaderWorkItemsClaimed = 0;
		shaderWorkItemsCompleted = 0;

		WRITE_BARRIER;

		// Absolute file path => checksum
		// Any file in the shaders directory not in this map still need to be compiled
		std::map<std::string, u64> compiledShaders;

		s_ShaderErrors.clear();

		if (!bForceCompileAll)
		{
			PROFILE_AUTO("Calculate shader contents checksum");

			const std::string shaderInputDirectory = SHADER_SOURCE_DIRECTORY;
			const std::string spvDirectoryAbs = RelativePathToAbsolute(COMPILED_SHADERS_DIRECTORY);

			if (FileExists(SHADER_CHECKSUM_LOCATION))
			{
				std::string fileContents;
				if (ReadFile(SHADER_CHECKSUM_LOCATION, fileContents, false))
				{
					std::vector<std::string> lines = Split(fileContents, '\n');

					for (const std::string& line : lines)
					{
						size_t midPoint = line.find('=');
						if (midPoint != std::string::npos && line.length() > 7) // Ignore degenerate lines
						{
							std::string filePath = Trim(line.substr(0, midPoint));

							if (filePath.length() > 0)
							{
								std::string storedChecksumStr = Trim(line.substr(midPoint + 1));
								u64 storedChecksum = std::stoull(storedChecksumStr);

								u64 calculatedChecksum = CalculteChecksum(filePath);

								std::string fileType = ExtractFileType(filePath);
								std::string fileName = StripLeadingDirectories(StripFileType(filePath));
								std::string compiledFileName = spvDirectoryAbs + fileName + "_" + fileType + ".spv";

								bool bFileExists = FileExists(compiledFileName);
								if (calculatedChecksum == storedChecksum && bFileExists)
								{
									compiledShaders.emplace(filePath, calculatedChecksum);
								}
							}
						}
					}
				}
			}
		}

		bSuccess = true;

		std::vector<std::string> filePaths;
		if (Platform::FindFilesInDirectory(SHADER_SOURCE_DIRECTORY, filePaths, s_RecognizedShaderTypes, ARRAY_LENGTH(s_RecognizedShaderTypes)))
		{
			startTime = Time::CurrentMilliseconds();

			for (const std::string& filePath : filePaths)
			{
				const std::string absoluteFilePath = RelativePathToAbsolute(filePath);
				std::string fileType = ExtractFileType(filePath);

				shaderc_shader_kind shaderKind = FilePathToShaderKind(fileType.c_str());
				if (shaderKind != (shaderc_shader_kind)-1)
				{
					const std::string shaderFileName = StripLeadingDirectories(filePath);

					if (!StartsWith(shaderFileName, "vk_"))
					{
						// TODO: EZ: Move vulkan shaders into their own directory
						continue;
					}

					if (compiledShaders.find(absoluteFilePath) != compiledShaders.end())
					{
						// Already compiled
						continue;
					}

					m_QueuedLoads.emplace_back(absoluteFilePath);
				}
			}
		}
		else
		{
			PrintError("Didn't find any shaders in %s!\n", SHADER_SOURCE_DIRECTORY);
			bSuccess = false;
			bComplete = true;
		}

		if (m_QueuedLoads.empty())
		{
			bComplete = true;
		}
		else
		{
			m_ThreadData.criticalSection = Platform::InitCriticalSection();
			m_ThreadData.bRunning = true;
			m_ThreadData.bGenerateAssembly = bGenerateAssembly;
			bComplete = false;

			if (s_ShaderCompilationResults != nullptr)
			{
				for (u32 i = 0; i < s_ShaderCompilationResultsLength; ++i)
				{
					delete s_ShaderCompilationResults[i];
				}

				delete[] s_ShaderCompilationResults;
				s_ShaderCompilationResults = nullptr;
			}

			WRITE_BARRIER;

			s_ShaderCompilationResultsLength = (u32)m_QueuedLoads.size();
			Print("Compiling %u shader%s...\n", s_ShaderCompilationResultsLength, (s_ShaderCompilationResultsLength == 1) ? "" : "s");
			s_ShaderCompilationResults = new ShaderCompilationResult * [s_ShaderCompilationResultsLength];
			for (u32 i = 0; i < s_ShaderCompilationResultsLength; ++i)
			{
				QueueWorkItem(m_QueuedLoads[i]);
			}

			u32 cpuCount = Platform::GetLogicalProcessorCount();
			u32 threadCount = glm::min(cpuCount / 2, s_ShaderCompilationResultsLength);
			for (u32 i = 0; i < threadCount; ++i)
			{
				m_Threads.emplace_back(std::thread(LoadShaderAsync, &m_ThreadData));
			}

			// Write valid checksums back to disk, skipping invalidated/missing entries
			std::string newChecksumFileContents;

			for (const auto& compiledShaderPair : compiledShaders)
			{
				newChecksumFileContents.append(compiledShaderPair.first + " = " + std::to_string(compiledShaderPair.second) + "\n");
			}

			if (newChecksumFileContents.length() > 0)
			{
				Platform::CreateDirectoryRecursive(ExtractDirectoryString(s_ChecksumFilePathAbs).c_str());
				std::ofstream checksumFileStream(s_ChecksumFilePathAbs, std::ios::out | std::ios::trunc);
				if (checksumFileStream.is_open())
				{
					checksumFileStream.write(newChecksumFileContents.c_str(), newChecksumFileContents.size());
					checksumFileStream.close();
				}
				else
				{
					PrintWarn("Failed to write shader checksum file to %s\n", SHADER_SOURCE_DIRECTORY);
				}
			}
		}
	}

	shaderc_shader_kind FilePathToShaderKind(const char* fileSuffix)
	{
		if (strcmp(fileSuffix, "vert") == 0) return shaderc_vertex_shader;
		if (strcmp(fileSuffix, "frag") == 0) return shaderc_fragment_shader;
		if (strcmp(fileSuffix, "geom") == 0) return shaderc_geometry_shader;
		if (strcmp(fileSuffix, "comp") == 0) return shaderc_compute_shader;

		return (shaderc_shader_kind)-1;
	}

	u64 ShaderCompiler::CalculteChecksum(const std::string& filePath)
	{
		u64 checksum = 0;

		const std::string fileName = StripLeadingDirectories(filePath);
		const std::string fileSuffix = ExtractFileType(filePath);
		bool bGoodStart = StartsWith(fileName, "vk_");
		bool bGoodEnd = Contains(s_RecognizedShaderTypes, ARRAY_SIZE(s_RecognizedShaderTypes), fileSuffix.c_str());
		if (bGoodStart && bGoodEnd)
		{
			std::string fileContents;
			if (FileExists(filePath) && ReadFile(filePath, fileContents, false))
			{
				const char* includeString = "include ";

				u32 i = 1;
				u32 fileContentsLen = (u32)fileContents.size();
				for (char c : fileContents)
				{
					if (c == '#' && i < (fileContentsLen + strlen(includeString)))
					{
						if (memcmp((void*)&fileContents[i], (void*)includeString, strlen(includeString)) == 0)
						{
							// Handle include
							bool bFoundInclude = false;

							size_t newLine = fileContents.find('\n', i);
							if (newLine != std::string::npos)
							{
								u32 includePathStart = i + (u32)strlen(includeString);
								std::string includePathRaw = fileContents.substr(includePathStart, newLine - includePathStart);
								includePathRaw = Trim(includePathRaw);

								if (includePathRaw.size() >= 3)
								{
									std::string includedPath;
									if (includePathRaw[0] == '"')
									{
										size_t endQuote = includePathRaw.find('"', 1);
										if (endQuote != std::string::npos)
										{
											includedPath = SHADER_SOURCE_DIRECTORY + includePathRaw.substr(1, endQuote - 1);
										}
									}
									else if (includePathRaw[0] == '<')
									{
										size_t endBracket = includePathRaw.find('>', 1);
										if (endBracket != std::string::npos)
										{
											includedPath = SHADER_SOURCE_DIRECTORY + includePathRaw.substr(1, endBracket - 1);
										}
									}

									if (!includedPath.empty() && FileExists(includedPath))
									{
										bFoundInclude = true;
										checksum += CalculteChecksum(includedPath);
									}
								}
							}

							if (!bFoundInclude)
							{
								PrintWarn("Invalid include directive in shader, ignoring.\n");
							}
						}
					}

					checksum += (u64)i * (u64)c;
					++i;
				}
			}
		}
		return checksum;
	}

	bool ShaderCompiler::TickStatus()
	{
		if (bComplete)
		{
			return false;
		}

		bool bAllCompleted = (shaderWorkItemsCreated == shaderWorkItemsCompleted);

		if (!bAllCompleted)
		{
			return false;
		}

		bComplete = true;

		OnAllCompilationsComplete();

		return true;
	}

	void ShaderCompiler::OnAllCompilationsComplete()
	{
		u32 compiledShaderCount = 0;
		u32 invalidShaderCount = 0;

		std::map<std::string, u64> compiledShaders;

		const std::string spvDirectoryAbs = RelativePathToAbsolute(COMPILED_SHADERS_DIRECTORY);

		m_ThreadData.bRunning = false;

		WRITE_BARRIER;

		for (std::thread& thread : m_Threads)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}

		for (u32 i = 0; i < s_ShaderCompilationResultsLength; ++i)
		{
			ShaderCompilationResult* compilationResult = s_ShaderCompilationResults[i];

			if (!compilationResult->errorStr.empty())
			{
				PrintError("%s\n", compilationResult->errorStr.c_str());
			}

			const std::string shaderFileName = StripLeadingDirectories(compilationResult->shaderAbsPath);

			std::string strippedFileName = StripFileType(shaderFileName);
			std::string fileType = ExtractFileType(shaderFileName);
			std::string newFileName = spvDirectoryAbs + strippedFileName + "_" + fileType;

			auto ParseLineNumber = [](const std::string& errorStr)
			{
				if (errorStr.empty())
				{
					return -1;
				}

				i32 lastColon = -1;
				i32 lineNumber = -1;
				const char* cStart = errorStr.data();
				const char* cCur = cStart;
				while (*cCur != '\0')
				{
					if (*cCur == ':')
					{
						i32 i = (i32)(cCur - cStart);
						if (lastColon == -1)
						{
							lastColon = i;
						}
						else
						{
							std::string sub = errorStr.substr(lastColon + 1, i - lastColon - 1);
							bool bValidSubStr = true;
							for (char c : sub)
							{
								if (c < '0' || c > '9')
								{
									bValidSubStr = false;
									break;
								}
							}

							if (bValidSubStr)
							{
								lineNumber = ParseInt(sub);
								break;
							}

							lastColon = i;
						}
					}

					++cCur;
				}

				return lineNumber;
			};

			auto ParseError = [&ParseLineNumber](ShaderCompilationResult* compilationResult, size_t numErrors, size_t numWarnings, const std::string& errorStr)
			{
				if (g_bEnableLogging_Shaders)
				{
					PrintError("%lu shader compilation errors, %lu warnings: \n", numErrors, numWarnings);
					PrintError("%s\n", errorStr.c_str());
				}

				u32 lineNumber = ParseLineNumber(errorStr);
				s_ShaderErrors.push_back({ errorStr, compilationResult->shaderAbsPath, lineNumber });
			};

			if (bGenerateAssembly)
			{
				if (compilationResult->assemblyResult.GetCompilationStatus() == shaderc_compilation_status_success)
				{
					std::vector<char> asmText(compilationResult->assemblyResult.begin(), compilationResult->assemblyResult.end());
					std::string asmFilePath = newFileName + ".asm";
					std::ofstream fileStream(asmFilePath, std::ios::out);
					if (fileStream.is_open())
					{
						fileStream.write((char*)asmText.data(), asmText.size() * sizeof(char));
						fileStream.close();
					}
					else
					{
						PrintError("Failed to write shader assembly to %s\n", asmFilePath.c_str());
						bSuccess = false;
					}
				}
				else
				{
					ParseError(compilationResult,
						compilationResult->assemblyResult.GetNumErrors(),
						compilationResult->assemblyResult.GetNumWarnings(),
						compilationResult->assemblyResult.GetErrorMessage());
				}
			}

			if (compilationResult->spvResult.GetCompilationStatus() == shaderc_compilation_status_success)
			{
				++compiledShaderCount;

				const u64 calculatedChecksum = CalculteChecksum(compilationResult->shaderAbsPath);
				compiledShaders.emplace(compilationResult->shaderAbsPath, calculatedChecksum);

				std::vector<u32> spvBytes(compilationResult->spvResult.begin(), compilationResult->spvResult.end());
				std::string spvFilePath = newFileName + ".spv";
				std::ofstream fileStream(spvFilePath, std::ios::out | std::ios::binary);
				if (fileStream.is_open())
				{
					fileStream.write((char*)spvBytes.data(), spvBytes.size() * sizeof(u32));
					fileStream.close();
				}
				else
				{
					PrintError("Failed to write compiled shader to %s\n", spvFilePath.c_str());
					++invalidShaderCount;
					bSuccess = false;
				}
			}
			else
			{
				ParseError(compilationResult,
					compilationResult->spvResult.GetNumErrors(),
					compilationResult->spvResult.GetNumWarnings(),
					compilationResult->spvResult.GetErrorMessage());

				++invalidShaderCount;
				bSuccess = false;
			}
		}

		m_Threads.clear();
		m_QueuedLoads.clear();

		if (m_ThreadData.criticalSection != nullptr)
		{
			Platform::FreeCriticalSection(m_ThreadData.criticalSection);
			m_ThreadData.criticalSection = nullptr;
		}

		shaderWorkItemsCreated = 0;
		shaderWorkItemsClaimed = 0;
		shaderWorkItemsCompleted = 0;

		// Append new checksums to file
		std::string checksumFileContentsAppend;
		for (const auto& compiledShaderPair : compiledShaders)
		{
			checksumFileContentsAppend.append(compiledShaderPair.first + " = " + std::to_string(compiledShaderPair.second) + "\n");
		}

		if ((compiledShaderCount > 0 || invalidShaderCount > 0) && checksumFileContentsAppend.length() > 0)
		{
			Platform::CreateDirectoryRecursive(ExtractDirectoryString(s_ChecksumFilePathAbs).c_str());
			std::ofstream checksumFileStream(s_ChecksumFilePathAbs, std::ios::out | std::ios::app);
			if (checksumFileStream.is_open())
			{
				checksumFileStream.write(checksumFileContentsAppend.c_str(), checksumFileContentsAppend.size());
				checksumFileStream.close();
			}
			else
			{
				PrintWarn("Failed to write shader checksum file to %s\n", SHADER_SOURCE_DIRECTORY);
			}
		}

		lastCompileDuration = Time::CurrentMilliseconds() - startTime;
		if (compiledShaderCount > 0)
		{
			Print("Compiled %u shader%s in %.1fms\n", compiledShaderCount, (compiledShaderCount > 1 ? "s" : ""), lastCompileDuration);
		}

		if (invalidShaderCount > 0)
		{
			PrintError("%u shader%s had errors\n", invalidShaderCount, (invalidShaderCount > 1 ? "s" : ""));
		}
	}

	void ShaderCompiler::JoinAllThreads()
	{
		for (std::thread& thread : m_Threads)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}

		OnAllCompilationsComplete();
	}

	i32 ShaderCompiler::WorkItemsRemaining() const
	{
		return shaderWorkItemsCreated - shaderWorkItemsCompleted;
	}

	i32 ShaderCompiler::ThreadCount() const
	{
		return (i32)m_Threads.size();
	}

	bool ShaderCompiler::WasShaderRecompiled(const char* shaderName) const
	{
		if (s_ShaderCompilationResultsLength == 0)
		{
			return false;
		}

		for (u32 i = 0; i < s_ShaderCompilationResultsLength; ++i)
		{
			ShaderCompilationResult* result = s_ShaderCompilationResults[i];

			std::string shaderFileName = RemovePrefix(StripFileType(StripLeadingDirectories(result->shaderAbsPath)), "vk_");
			if (strcmp(shaderFileName.c_str(), shaderName) == 0)
			{
				return true;
			}
		}

		return false;
	}
} // namespace flex
#endif //  COMPILE_SHADER_COMPILER
