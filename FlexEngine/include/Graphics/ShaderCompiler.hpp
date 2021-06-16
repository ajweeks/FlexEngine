#pragma once
#if COMPILE_SHADER_COMPILER

IGNORE_WARNINGS_PUSH
#if COMPILE_SHADER_COMPILER
#include "shaderc/shaderc.h" // For shaderc_shader_kind
#include "shaderc/shaderc.hpp" // For IncluderInterface
#endif
IGNORE_WARNINGS_POP

#include "Helpers.hpp"

namespace flex
{
	class FlexShaderIncluder : public shaderc::CompileOptions::IncluderInterface
	{
		virtual shaderc_include_result* GetInclude(const char* requested_source,
			shaderc_include_type type,
			const char* requesting_source,
			size_t include_depth) override
		{
			FLEX_UNUSED(type);
			FLEX_UNUSED(requesting_source);
			FLEX_UNUSED(include_depth);

			shaderc_include_result* result = new shaderc_include_result();

			std::string requestedFilePath = SHADER_SOURCE_DIRECTORY + std::string(requested_source);
			std::string fileContent;
			if (FileExists(requestedFilePath) && ReadFile(requestedFilePath, fileContent, false))
			{
				u32 requestedFilePathLen = (u32)requestedFilePath.size();
				result->source_name = (const char*)malloc(requestedFilePathLen + 1);
				memset((void*)result->source_name, 0, requestedFilePathLen + 1);
				strncpy((char*)result->source_name, requestedFilePath.c_str(), requestedFilePathLen);
				result->source_name_length = requestedFilePathLen;

				u32 fileContentLen = (u32)fileContent.size();
				result->content = (const char*)malloc(fileContentLen + 1);
				memset((void*)result->content, 0, fileContentLen + 1);
				strncpy((char*)result->content, fileContent.c_str(), fileContentLen);
				result->content_length = strlen(result->content);
			}
			else
			{
				result->source_name = "";
				result->source_name_length = 0;
				result->content = "Failed to include shader";
				result->content_length = strlen(result->content);
			}

			return result;
		}

		virtual void ReleaseInclude(shaderc_include_result* data) override
		{
			// This causes heap corruption for some reason... but it's leaking without this.
			//if (data->source_name_length != 0)
			//{
			//	free((void*)data->source_name);
			//	free((void*)data->content);
			//}
			delete data;
		}
	};

	struct ShaderCompiler
	{
		ShaderCompiler();
		~ShaderCompiler();

		ShaderCompiler(const ShaderCompiler&) = delete;
		ShaderCompiler(const ShaderCompiler&&) = delete;
		ShaderCompiler& operator=(const ShaderCompiler&) = delete;
		ShaderCompiler& operator=(const ShaderCompiler&&) = delete;

		static void ClearShaderHash(const std::string& shaderName);

		static void DrawImGuiShaderErrorsWindow(bool* bWindowShowing);
		static void DrawImGuiShaderErrors();

		void StartCompilation(bool bForceCompileAll = false);
		bool TickStatus(); // Returns true on the frame that all shader compilations have completed
		void JoinAllThreads();

		i32 WorkItemsRemaining() const;
		i32 ThreadCount() const;

		bool WasShaderRecompiled(const char* shaderName) const;

		// Whether a textual assembly version should also be generated (necessary for reflection - see Renderer::ParseShaderMetaData)
		const bool bGenerateAssembly = true;

		ms startTime = 0.0f;
		ms lastCompileDuration = 0.0f;

		bool bSuccess = true;
		bool bComplete = true;

		struct ShaderError
		{
			std::string errorStr;
			std::string filePath;
			u32 lineNumber;
		};

		struct ShaderCompilationResult
		{
			bool bSuccess = false;
			std::string shaderAbsPath;
			shaderc::AssemblyCompilationResult assemblyResult;
			shaderc::SpvCompilationResult spvResult;
			std::string errorStr; // Used for additional errors besides those in result
		};

		std::vector<std::thread> m_Threads;
		std::vector<std::string> m_QueuedLoads;
		ShaderThreadData m_ThreadData;

	private:
		static std::string s_ChecksumFilePathAbs;
		static const char* s_RecognizedShaderTypes[];

		void QueueWorkItem(const std::string& shaderAbsPath);
		void OnAllCompilationsComplete();

		u64 CalculteChecksum(const std::string& filePath);

	};

	shaderc_shader_kind FilePathToShaderKind(const char* fileSuffix);
} // namespace flex

#endif // COMPILE_SHADER_COMPILER
