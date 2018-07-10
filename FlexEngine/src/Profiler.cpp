#include "stdafx.hpp"

#include "Profiler.hpp"

#include "Time.hpp"
#include "Graphics/Renderer.hpp"

namespace flex
{
	i32 Profiler::s_UnendedTimings = 0;
	ms Profiler::s_FrameStartTime = 0;
	ms Profiler::s_FrameEndTime = 0;
	std::unordered_map<u64, Profiler::Timing> Profiler::s_Timings;
	std::string Profiler::s_PendingCSV;
	Profiler::DisplayedFrameOptions Profiler::s_DisplayedFrameOptions;
	std::vector<Profiler::Timing> Profiler::s_DisplayedFrameTimings;

	// Any frame time longer than this will be clipped to this value
	const ms MAX_FRAME_TIME = 100;

	void Profiler::StartFrame()
	{
		s_Timings.clear();
		s_UnendedTimings = 0;
		s_FrameStartTime = Time::CurrentMilliseconds();
	}

	void Profiler::EndFrame(bool bUpdateVisualFrame)
	{
		s_FrameEndTime = Time::CurrentMilliseconds();

		if (s_UnendedTimings != 0)
		{
			PrintError("Uneven number of profile blocks! (%i)\n", s_UnendedTimings);
		}

		ms frameDuration = glm::min(s_FrameEndTime - s_FrameStartTime, MAX_FRAME_TIME);
		s_PendingCSV.append(std::to_string(frameDuration) + ",");

		//Print("Profiler results:");
		//Print("Whole frame: " + std::to_string(s_FrameEndTime - s_FrameStartTime) + "ms");
		//Print("---");
		//for (auto& element : s_Timings)
		//{
			//s_PendingCSV.append(std::string(element.first) + "," +
			//					std::to_string(element.second) + '\n');

			//Print(std::string(element.first) + ": " + 
			//				std::to_string(element.second) + "ms");
		//}

		if (bUpdateVisualFrame)
		{
			s_DisplayedFrameTimings.clear();

			// First element is always total frame timing
			Timing frameTiming = {};
			frameTiming.start = s_FrameStartTime;
			frameTiming.end = s_FrameEndTime;
			s_DisplayedFrameTimings.emplace_back(frameTiming);

			for (auto timingPair : s_Timings)
			{
				if (timingPair.second.start >= s_FrameStartTime)
				{
					assert(timingPair.second.end <= s_FrameEndTime);

					s_DisplayedFrameTimings.emplace_back(timingPair.second);
				}
			}
		}
	}

	void Profiler::Begin(const char* blockName)
	{
		assert(strlen(blockName) <= Timing::MAX_NAME_LEN);

		u64 hash = Hash(blockName);

		if (s_Timings.find(hash) != s_Timings.end())
		{
			PrintError("Profiler::Begin called more than once! Block name: %s (hash: %i)\n",
					   blockName, hash);
		}

		ms now = Time::CurrentMilliseconds();
		Timing timing = {};
		timing.start = now;
		timing.end = real_min;
		strcpy_s(timing.blockName, blockName);
		s_Timings.insert({ hash, timing });

		++s_UnendedTimings;
	}

	void Profiler::Begin(const std::string& blockName)
	{
		Begin(blockName.c_str());
	}

	void Profiler::End(const char* blockName)
	{
		u64 hash = Hash(blockName);

		auto iter = s_Timings.find(hash);
		if (iter == s_Timings.end())
		{
			PrintError("Profiler::End called before Begin was called! Block name: %s (hash: %i)\n", 
					   blockName, hash);
			return;
		}

		assert(strcmp(iter->second.blockName, blockName) == 0);

		ms now = Time::CurrentMilliseconds();
		iter->second.end = now;

		--s_UnendedTimings;
	}

	void Profiler::End(const std::string& blockName)
	{
		End(blockName.c_str());
	}

	void Profiler::PrintResultsToFile()
	{
		if (s_PendingCSV.empty())
		{
			PrintWarn("Attempted to print profiler results to file before any results were generated!"
					  "Did you set bPrintTimings when calling EndFrame?\n");
			return;
		}

		std::string directory = RESOURCE_LOCATION + "profiles/";
		std::string absoluteDirectory = RelativePathToAbsolute(directory);
		CreateDirectoryRecursive(absoluteDirectory);

		std::string filePath = absoluteDirectory + "frame_times.csv";

		if (WriteFile(filePath, s_PendingCSV, false))
		{
			Print("Wrote profiling results to %s\n", filePath.c_str());
		}
		else
		{
			Print("Failed to write profiling results to %s\n", filePath.c_str());
		}
	}

	void Profiler::PrintBlockDuration(const char* blockName)
	{
		u64 hash = Hash(blockName);

		auto iter = s_Timings.find(hash);
		if (iter != s_Timings.end())
		{
			ms duration = (iter->second.end - iter->second.start);
			Print("    Block duration \"%s\": %.2fms\n", blockName, duration);
		}
	}

	void Profiler::PrintBlockDuration(const std::string& blockName)
	{
		PrintBlockDuration(blockName.c_str());
	}

	void Profiler::DrawDisplayedFrame()
	{
		if (s_DisplayedFrameTimings.empty())
		{
			return;
		}

		glm::vec2i frameBufferSize = g_Window->GetFrameBufferSize();
		real aspectRatio = frameBufferSize.x / (real)frameBufferSize.y;
		{
			//glm::vec2 pos = g_InputManager->GetMousePosition() / (glm::vec2)frameBufferSize;
			//pos.y = 1.0f - pos.y;
			//pos *= 2.0f;
			//pos -= glm::vec2(1.0f);
			//pos.x *= aspectRatio;
			//g_Renderer->DrawUntexturedQuadRaw(pos,
			//								  glm::vec2(0.01f),
			//								  glm::vec4(1.0f));

			
			/*
			Sprite space to pixel space:
			- Divide x by aspect ratio
			- + 1
			- / 2
			- y = 1 - y
			- * frameBufferSize
			*/
		}

		g_Renderer->SetFont(g_Renderer->m_FntSourceCodePro);

		i32 blockCount = (i32)s_DisplayedFrameTimings.size();
		ms frameStart = s_DisplayedFrameTimings[0].start;
		ms frameEnd = s_DisplayedFrameTimings[0].end;

		ms frameDuration = frameEnd - frameStart;

		const glm::vec2 frameScale(s_DisplayedFrameOptions.screenWidthPercent,
								  s_DisplayedFrameOptions.screenHeightPercent);
		const glm::vec2 framePos(s_DisplayedFrameOptions.xOffPercent,
								 s_DisplayedFrameOptions.yOffPercent);
		real blockHeight = frameScale.y / (real)blockCount;

		glm::vec4 frameColor(1.0f, 1.0f, 1.0f, s_DisplayedFrameOptions.opacity);

		g_Renderer->DrawUntexturedQuad(framePos, AnchorPoint::CENTER, frameScale, frameColor);
		std::string frameDurationStr = FloatToString(frameDuration, 2) + "ms";
		g_Renderer->DrawString(frameDurationStr, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
							   AnchorPoint::CENTER, glm::vec2(framePos.x, framePos.y - frameScale.y * 1.1f), 5,
							   false);


		real a = s_DisplayedFrameOptions.opacity;
		glm::vec4 blockColors[] = {
			glm::vec4(0.43f, 0.48f, 0.58f, a), // Pale dark blue
			glm::vec4(0.50f, 0.58f, 0.31f, a), // Pale green
			glm::vec4(0.13f, 0.08f, 0.26f, a), // Royal purple
			glm::vec4(0.17f, 0.98f, 0.95f, a), // Cyan
			glm::vec4(0.95f, 0.95f, 0.65f, a), // Light yellow
			glm::vec4(0.85f, 0.68f, 0.95f, a), // Light purple
			glm::vec4(0.95f, 0.18f, 0.55f, a), // Dark pink
			glm::vec4(0.95f, 0.68f, 0.28f, a), // Orange
			glm::vec4(0.60f, 0.95f, 0.33f, a), // Lime
			glm::vec4(0.58f, 0.95f, 0.65f, a), // Light green
			glm::vec4(0.95f, 0.13f, 0.13f, a), // Red
			glm::vec4(0.44f, 0.92f, 0.95f, a), // Light cyan
			glm::vec4(0.95f, 0.95f, 0.25f, a), // Yellow
			glm::vec4(0.40f, 0.63f, 0.95f, a), // Light blue
			glm::vec4(0.90f, 0.55f, 0.70f, a), // Pink
			glm::vec4(0.15f, 0.16f, 0.95f, a), // Dark blue
			glm::vec4(0.70f, 0.45f, 0.95f, a), // Purple
		};
		i32 colorIndex = 0;
		for (i32 i = 1; i < blockCount; ++i)
		{
			Timing& timing = s_DisplayedFrameTimings[i];

			ms blockDuration = timing.end - timing.start;
			real blockWidth = blockDuration / frameDuration;
			real blockStartX = (framePos.x) + ((timing.start - frameStart) / frameDuration) * frameScale.x;
			real blockStartY = framePos.y + frameScale.y - blockHeight / 2.0f;
			glm::vec2 blockScale(frameScale.x * blockWidth,
								blockHeight);
			glm::vec2 blockPos(blockStartX - blockWidth/2.0f,
							   blockStartY - (i / (real)blockCount) * frameScale.y * 2.0f);
			blockPos.y *= -1;

			//glm::vec2 normalizedBlockScale;
			//g_Renderer->NormalizeSpritePos(glm::vec2(blockPos.x - 0.5f, blockPos.y * 0.5f - blockHeight),
			//							   AnchorPoint::CENTER,
			//							   glm::vec2(blockScale.x * 0.5f, blockScale.y * 1.0f),
			//							   normalizedBlockPos, normalizedBlockScale);
			bool bMouseHovered = g_InputManager->IsMouseHoveringArea(blockPos, blockScale / glm::vec2(aspectRatio, 1.0f));

			g_Renderer->DrawUntexturedQuadRaw(blockPos, blockScale, blockColors[colorIndex]);

			if (bMouseHovered)
			{
				g_Renderer->DrawUntexturedQuadRaw(blockPos, blockScale, glm::vec4(1.0f, 1.0f, 1.0f, 0.3f));

				blockPos.y *= -1;
				g_Renderer->DrawString(timing.blockName,
									   glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
									   AnchorPoint::CENTER,
									   glm::vec2(frameScale.x*1.01f / aspectRatio, blockPos.y),
									   5, 
									   true);
			}

			++colorIndex;
			colorIndex %= ARRAY_SIZE(blockColors);
		}
	}

	u64 Profiler::Hash(const char* str)
	{
		size_t result = std::hash<std::string>{}(str);
		
		return (u64)result;
	}
} // namespace flex
