#pragma once

namespace flex
{
	struct Histogram
	{
		Histogram()
		{
		}

		explicit Histogram(i32 count)
		{
			data.resize(count);
		}

		void Clear()
		{
			data.clear();
		}

		void AddElement(real element)
		{
			data[index] = element;
			index += 1;
			index %= data.size();
		}

		void DrawImGui()
		{
			ImVec2 p = ImGui::GetCursorScreenPos();
			real width = 300.0f;
			real height = 100.0f;
			for (real element : data)
			{
				lowestMin = glm::min(lowestMin, element);
				highestMax = glm::max(highestMax, element);
			}
			real maxAbs = glm::max(abs(lowestMin), abs(highestMax));
			ImGui::PlotLines("", data.data(), (u32)data.size(), 0, 0, -maxAbs, maxAbs, ImVec2(width, height));
			ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + height * 0.5f), ImVec2(p.x + width, p.y + height * 0.5f), IM_COL32(0, 128, 0, 255), 1.0f);
			p.x += width * ((real)index / data.size());
			ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x, p.y + height), IM_COL32(128, 0, 0, 255), 1.0f);
		}

		std::vector<real> data;
		i32 index = 0;
		real lowestMin = FLT_MAX;
		real highestMax = -FLT_MAX;
	};

} // namespace flex
