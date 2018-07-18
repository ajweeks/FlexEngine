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
	const real Profiler::s_ScrollSpeed = 0.4f;
	bool Profiler::s_bDisplayingFrame = false;

	glm::vec4 Profiler::blockColors[] = {
		glm::vec4(0.43f, 0.48f, 0.58f, 0.8f), // Pale dark blue
		glm::vec4(0.50f, 0.58f, 0.31f, 0.8f), // Pale green
		glm::vec4(0.45f, 0.33f, 0.82f, 0.8f), // Royal purple
		glm::vec4(0.17f, 0.98f, 0.95f, 0.8f), // Cyan
		glm::vec4(0.95f, 0.95f, 0.65f, 0.8f), // Light yellow
		glm::vec4(0.85f, 0.68f, 0.95f, 0.8f), // Light purple
		glm::vec4(0.95f, 0.18f, 0.55f, 0.8f), // Dark pink
		glm::vec4(0.95f, 0.68f, 0.28f, 0.8f), // Orange
		glm::vec4(0.60f, 0.95f, 0.33f, 0.8f), // Lime
		glm::vec4(0.58f, 0.95f, 0.65f, 0.8f), // Light green
		glm::vec4(0.95f, 0.13f, 0.13f, 0.8f), // Red
		glm::vec4(0.44f, 0.92f, 0.95f, 0.8f), // Light cyan
		glm::vec4(0.95f, 0.95f, 0.25f, 0.8f), // Yellow
		glm::vec4(0.40f, 0.63f, 0.95f, 0.8f), // Light blue
		glm::vec4(0.90f, 0.55f, 0.70f, 0.8f), // Pink
		glm::vec4(0.25f, 0.26f, 0.97f, 0.8f), // Dark blue
		glm::vec4(0.70f, 0.45f, 0.95f, 0.8f), // Purple
	};

	// Any frame time longer than this will be clipped to this value
	const ms MAX_FRAME_TIME = 100;

	void Profiler::StartFrame()
	{
		s_Timings.clear();
		s_UnendedTimings = 0;
		s_FrameStartTime = Time::CurrentMilliseconds();
	}

	void Profiler::Update()
	{
		if (s_bDisplayingFrame)
		{
			const glm::vec2 frameSizeHalf(s_DisplayedFrameOptions.screenWidthPercent * s_DisplayedFrameOptions.hZoom,
										  s_DisplayedFrameOptions.screenHeightPercent);
			const glm::vec2 frameCenter = glm::vec2(s_DisplayedFrameOptions.xOffPercent + s_DisplayedFrameOptions.hScroll + s_DisplayedFrameOptions.hO,
													s_DisplayedFrameOptions.yOffPercent);

			bool bMouseHovered = g_InputManager->IsMouseHoveringRect(frameCenter, frameSizeHalf);
			if (bMouseHovered)
			{
				real scrollDist = g_InputManager->GetVerticalScrollDistance();
				if (scrollDist != 0.0f)
				{
					s_DisplayedFrameOptions.hZoom += scrollDist * s_ScrollSpeed;
					real minZoom = 0.5f;
					real maxZoom = 4.0f;
					s_DisplayedFrameOptions.hZoom = glm::clamp(s_DisplayedFrameOptions.hZoom, minZoom, maxZoom);
					if (scrollDist < 0.0f)
					{
						real a = 1.0f - (s_DisplayedFrameOptions.hZoom - minZoom) / (maxZoom - minZoom);
						s_DisplayedFrameOptions.hO = Lerp(s_DisplayedFrameOptions.hO, 0.0f, a);
					}
					else
					{
						real off = -(g_InputManager->GetMousePosition().x / (real)g_Window->GetFrameBufferSize().x - 0.5f) * 2.0f;
						real a = (1.0f - (s_DisplayedFrameOptions.hZoom - minZoom) / (maxZoom - minZoom)) *  s_ScrollSpeed;
						s_DisplayedFrameOptions.hO += Lerp(0.0f, off, a);
					}
					// TODO: HACK: Add proper input consumption code
					g_InputManager->ClearVerticalScrollDistance();
				}

				real hDragDist = g_InputManager->GetMouseDragDistance(InputManager::MouseButton::LEFT).x;
				if (g_InputManager->IsMouseButtonReleased(InputManager::MouseButton::LEFT))
				{
					s_DisplayedFrameOptions.hO += s_DisplayedFrameOptions.hScroll;
					s_DisplayedFrameOptions.hScroll = 0;
				}
				if (g_InputManager->IsMouseButtonDown(InputManager::MouseButton::LEFT) &&
					hDragDist != 0.0f)
				{
					s_DisplayedFrameOptions.hScroll = hDragDist * 0.001f;
					// TODO: HACK: Add proper input consumption code
					g_InputManager->ClearMouseMovement();
				}
			}
		}
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
			strcpy_s(frameTiming.blockName, "Total frame time");
			s_DisplayedFrameTimings.emplace_back(frameTiming);

			for (auto timingPair : s_Timings)
			{
				assert(timingPair.second.start >= s_FrameStartTime);
				assert(timingPair.second.end <= s_FrameEndTime);

				s_DisplayedFrameTimings.emplace_back(timingPair.second);
			}

			std::sort(s_DisplayedFrameTimings.begin(), s_DisplayedFrameTimings.end(), [](Timing& a, Timing& b)
			{
				return a.start < b.start;
			});
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
		if (!s_bDisplayingFrame ||
			s_DisplayedFrameTimings.empty())
		{
			return;
		}

		BitmapFont* font = g_Renderer->m_FntSourceCodePro;
		g_Renderer->SetFont(font);

		i32 blockCount = (i32)s_DisplayedFrameTimings.size();
		ms frameStart = s_DisplayedFrameTimings[0].start;
		ms frameEnd = s_DisplayedFrameTimings[0].end;

		ms frameDuration = frameEnd - frameStart;

		const glm::vec2 frameSizeHalf(s_DisplayedFrameOptions.screenWidthPercent * s_DisplayedFrameOptions.hZoom, 
									  s_DisplayedFrameOptions.screenHeightPercent );
		const glm::vec2 frameCenter = glm::vec2(s_DisplayedFrameOptions.xOffPercent + s_DisplayedFrameOptions.hScroll + s_DisplayedFrameOptions.hO,
												s_DisplayedFrameOptions.yOffPercent);

		const glm::vec2 frameTL = frameCenter + glm::vec2(-frameSizeHalf.x, frameSizeHalf.y);

		g_Renderer->DrawUntexturedQuadRaw(frameCenter,
										  frameSizeHalf,
										  glm::vec4(0.175f, 0.175f, 0.175f, s_DisplayedFrameOptions.opacity));

		bool bMouseHoveredOverMainFrame = g_InputManager->IsMouseHoveringRect(frameCenter, frameSizeHalf);
		if (bMouseHoveredOverMainFrame)
		{
			// Highlight frame with translucent white quad when hovered
			g_Renderer->DrawUntexturedQuadRaw(frameCenter,
											  frameSizeHalf,
											  glm::vec4(1.0f, 1.0f, 1.0f, 0.03f));
		}
		std::string frameDurationStr = FloatToString(frameDuration, 2) + "ms";
		real letterSpacing = 5;
		real durationStrWidth = g_Renderer->GetStringWidth(frameDurationStr, font, letterSpacing, true);
		g_Renderer->DrawString(frameDurationStr,
							   glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
							   AnchorPoint::CENTER,
							   glm::vec2(frameCenter.x - durationStrWidth, frameCenter.y + frameSizeHalf.y * 1.1f),
							   letterSpacing,
							   true);

		real blockHeight = (frameSizeHalf.y / ((real)blockCount + 2));

		i32 colorIndex = 0;
		for (i32 i = 0; i < blockCount; ++i)
		{
			Timing& timing = s_DisplayedFrameTimings[i];

			ms blockDuration = timing.end - timing.start;
			real halfBlockWidth = (blockDuration / frameDuration) * frameSizeHalf.x;
			real blockLeftX = (frameTL.x) + ((timing.start - frameStart) / frameDuration) * frameSizeHalf.x * 2.0f;
			real blockCenterY = frameTL.y - blockHeight - ((i + 0.5f) / ((real)blockCount + 1.0f)) * frameSizeHalf.y * 2.0f;
			glm::vec2 blockCenterNorm(blockLeftX + halfBlockWidth, blockCenterY);
			glm::vec2 blockScaleNorm(halfBlockWidth, blockHeight);

			g_Renderer->DrawUntexturedQuadRaw(blockCenterNorm, blockScaleNorm, blockColors[colorIndex]);

			bool bMouseHoveredOverBlock = g_InputManager->IsMouseHoveringRect(blockCenterNorm, blockScaleNorm);
			if (bMouseHoveredOverBlock)
			{
				// Highlight hovered block with translucent white quad
				g_Renderer->DrawUntexturedQuadRaw(blockCenterNorm, blockScaleNorm, glm::vec4(1.0f, 1.0f, 1.0f, 0.3f));


				glm::vec2 frameBufferSize = (glm::vec2)g_Window->GetFrameBufferSize();
				real aspectRatio = frameBufferSize.x / frameBufferSize.y;

				std::string str = timing.blockName;
				real strWidth = g_Renderer->GetStringWidth(str, font, letterSpacing, true);
				glm::vec2 pos(blockCenterNorm.x - strWidth * aspectRatio, frameCenter.y - frameSizeHalf.y * 1.2f);
				g_Renderer->DrawString(timing.blockName,
									   glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
									   AnchorPoint::CENTER,
									   pos,
									   letterSpacing,
									   true);
				str = FloatToString(blockDuration, 2) + "ms";
				strWidth = g_Renderer->GetStringWidth(str, font, letterSpacing, true);
				pos.x = blockCenterNorm.x - strWidth * aspectRatio;
				pos.y -= 0.05f;
				g_Renderer->DrawString(str,
									   glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
									   AnchorPoint::CENTER,
									   pos ,
									   letterSpacing,
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
