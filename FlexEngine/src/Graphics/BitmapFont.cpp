#include "stdafx.hpp"

#include "Graphics/BitmapFont.hpp"

#include "Graphics/RendererTypes.hpp"
#include "Systems/JobSystem.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

namespace flex
{
	BitmapFont::BitmapFont(const FontMetaData& inMetaData, const std::string& name, i32 charCount) :
		name(name),
		characterCount(charCount),
		metaData(inMetaData)
	{
		metaData.bitmapFont = this;
		CHECK_GT(metaData.size, 0);
		CHECK_GT(charCount, 0);
	}

	BitmapFont::~BitmapFont()
	{
		delete m_Texture;
		m_Texture = nullptr;
	}

	std::wstring BitmapFont::DecodeUTF8(const std::string& str)
	{
		std::wstring result;
		result.reserve(str.size());
		for (size_t i = 0; i < str.size(); ++i)
		{
			u8 b = (u8)str[i];
			if (b >= 0xC0 && b < 0xE0 && i + 1 < str.size() && ((u8)str[i + 1] & 0xC0) == 0x80)
			{
				result.push_back((wchar_t)(((b & 0x1F) << 6) | ((u8)str[i + 1] & 0x3F)));
				++i;
			}
			else
			{
				result.push_back((wchar_t)b);
			}
		}
		return result;
	}

	bool BitmapFont::IsCharValid(wchar_t character)
	{
		return character < CHAR_COUNT;
	}

	void BitmapFont::SetMetric(const FontMetric& metric, wchar_t character)
	{
		m_CharTable[character] = metric;
	}

	FontMetric* BitmapFont::GetMetric(wchar_t character)
	{
		return &m_CharTable[character];
	}

	real BitmapFont::GetKerning(wchar_t leftChar, wchar_t rightChar) const
	{
		if (!bUseKerning)
		{
			return 0.0f;
		}

		auto iter = m_Kerning.find(((u32)leftChar << 16) | (u32)rightChar);
		return iter != m_Kerning.end() ? iter->second : 0.0f;
	}

	real BitmapFont::GetTracking(real pixelsPerUnit) const
	{
		if (pixelsPerUnit <= 0.0f)
		{
			return 0.0f;
		}
		// Up to 0.6 pixels for an em of 12 pixels or less, fading out by 24 pixels
		const real emPixels = emSize * pixelsPerUnit;
		const real trackingPixels = 0.6f * glm::clamp((24.0f - emPixels) / 12.0f, 0.0f, 1.0f);
		return trackingPixels / pixelsPerUnit;
	}

	void BitmapFont::SetKerning(wchar_t leftChar, wchar_t rightChar, real kerning)
	{
		m_Kerning[((u32)leftChar << 16) | (u32)rightChar] = kerning;
		bUseKerning = true;
	}

	void BitmapFont::LoadKerning(const std::vector<char>& fileMemory, real fontUnitsToLayout, const std::map<i32, FontMetric*>& characters)
	{
		// FreeType only reads the legacy 'kern' table, but most modern fonts (e.g. Bahnschrift) only have GPOS kerning,
		// which stb_truetype can read
		const unsigned char* data = (const unsigned char*)fileMemory.data();
		stbtt_fontinfo info;
		if (!stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0)))
		{
			return;
		}

		for (const auto& left : characters)
		{
			for (const auto& right : characters)
			{
				i32 kerning = stbtt_GetCodepointKernAdvance(&info, left.first, right.first);
				if (kerning != 0)
				{
					SetKerning((wchar_t)left.first, (wchar_t)right.first, kerning * fontUnitsToLayout);
				}
			}
		}
	}

	// Felzenszwalb & Huttenlocher's 1D squared euclidean distance transform, applied in place to a row or column of grid
	static void DistanceTransform1D(real* grid, i32 offset, i32 stride, i32 length, real* f, real* z, i32* v)
	{
		const real INF = 1e20f;
		f[0] = grid[offset];
		v[0] = 0;
		z[0] = -INF;
		z[1] = INF;
		for (i32 q = 1, k = 0; q < length; ++q)
		{
			f[q] = grid[offset + q * stride];
			real s;
			do
			{
				const i32 r = v[k];
				s = (f[q] - f[r] + (real)(q * q - r * r)) / (real)(2 * (q - r));
			} while (s <= z[k] && --k > -1);
			++k;
			v[k] = q;
			z[k] = s;
			z[k + 1] = INF;
		}
		for (i32 q = 0, k = 0; q < length; ++q)
		{
			while (z[k + 1] < (real)q)
			{
				++k;
			}
			const i32 r = v[k];
			grid[offset + q * stride] = f[r] + (real)((q - r) * (q - r));
		}
	}

	static void DistanceTransform2D(real* grid, i32 width, i32 height, real* f, real* z, i32* v)
	{
		for (i32 x = 0; x < width; ++x)
		{
			DistanceTransform1D(grid, x, width, height, f, z, v);
		}
		for (i32 y = 0; y < height; ++y)
		{
			DistanceTransform1D(grid, y * width, 1, width, f, z, v);
		}
	}

	void BitmapFont::GenerateSDFAtlas(::FT_FaceRec_* face, BitmapFont* font, const std::map<i32, FontMetric*>& characters,
		const glm::vec2i& atlasSize, std::vector<u8>& outPixels)
	{
		PROFILE_AUTO("GenerateSDFAtlas");

		const i32 S = (i32)SDF_SUPERSAMPLE;
		const i32 renderScale = (i32)font->atlasScale * S;
		const real spreadAtlas = (real)(SDF_SPREAD * font->atlasScale);
		const real INF = 1e20f;

		outPixels.assign((size_t)atlasSize.x * atlasSize.y * 4, 0);

		// Render each glyph's coverage with FreeType (which isn't thread safe), then compute their SDFs in parallel
		struct GlyphBitmap
		{
			FontMetric* metric;
			i32 left;
			i32 top;
			i32 width;
			i32 rows;
			std::vector<u8> coverage;
		};
		std::vector<GlyphBitmap> glyphs;
		glyphs.reserve(characters.size());

		for (const auto& charPair : characters)
		{
			FontMetric* metric = charPair.second;
			if (metric->atlasWidth == 0 || metric->atlasHeight == 0)
			{
				continue;
			}

			u32 glyphIndex = FT_Get_Char_Index(face, metric->character);
			if (glyphIndex == 0 ||
				FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != FT_Err_Ok ||
				FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != FT_Err_Ok)
			{
				PrintError("Failed to render glyph %u of font %s\n", glyphIndex, font->name.c_str());
				continue;
			}

			const FT_Bitmap& bitmap = face->glyph->bitmap;
			GlyphBitmap glyph = { metric, face->glyph->bitmap_left, face->glyph->bitmap_top, (i32)bitmap.width, (i32)bitmap.rows, {} };
			glyph.coverage.resize((size_t)glyph.width * glyph.rows);
			for (i32 row = 0; row < glyph.rows; ++row)
			{
				memcpy(glyph.coverage.data() + (size_t)row * glyph.width, bitmap.buffer + row * bitmap.pitch, glyph.width);
			}
			glyphs.push_back(std::move(glyph));
		}

		JobSystem::Context context;
		JobSystem::Dispatch(context, (u32)glyphs.size(), 1, [&](JobSystem::JobArgs args)
		{
			const GlyphBitmap& glyph = glyphs[args.jobIndex];
			const FontMetric* metric = glyph.metric;

			// High resolution coverage grid covering the glyph's whole atlas cell
			const i32 gw = (i32)metric->atlasWidth * S;
			const i32 gh = (i32)metric->atlasHeight * S;
			// Top left corner of the cell in render pixels, relative to the pen position (y up)
			const i32 originX = (i32)std::lround(metric->offsetX * renderScale);
			const i32 originY = (i32)std::lround(-metric->offsetY * renderScale);

			// Squared distances to the nearest outside (outer) & inside (inner) pixel, initialized from coverage at edges
			std::vector<real> outer((size_t)gw * gh, INF);
			std::vector<real> inner((size_t)gw * gh, 0.0f);
			for (i32 row = 0; row < glyph.rows; ++row)
			{
				const i32 gy = originY - glyph.top + row;
				if (gy < 0 || gy >= gh)
				{
					continue;
				}
				const u8* src = glyph.coverage.data() + (size_t)row * glyph.width;
				for (i32 col = 0; col < glyph.width; ++col)
				{
					const i32 gx = glyph.left + col - originX;
					if (gx < 0 || gx >= gw || src[col] == 0)
					{
						continue;
					}
					const size_t idx = (size_t)gy * gw + gx;
					if (src[col] == 255)
					{
						outer[idx] = 0.0f;
						inner[idx] = INF;
					}
					else
					{
						const real d = 0.5f - src[col] / 255.0f;
						outer[idx] = d > 0.0f ? d * d : 0.0f;
						inner[idx] = d < 0.0f ? d * d : 0.0f;
					}
				}
			}

			const i32 maxDim = glm::max(gw, gh);
			std::vector<real> f(maxDim);
			std::vector<real> z(maxDim + 1);
			std::vector<i32> v(maxDim);
			DistanceTransform2D(outer.data(), gw, gh, f.data(), z.data(), v.data());
			DistanceTransform2D(inner.data(), gw, gh, f.data(), z.data(), v.data());

			// Signed distance (in render pixels, positive outside) at the center of high res pixel (x, y)
			auto SignedDist = [&](i32 x, i32 y)
			{
				x = glm::clamp(x, 0, gw - 1);
				y = glm::clamp(y, 0, gh - 1);
				const size_t idx = (size_t)y * gw + x;
				return sqrtf(outer[idx]) - sqrtf(inner[idx]);
			};

			// Cells only overlap in other channels, so jobs never write the same bytes
			const glm::vec2i cellPos = glm::vec2i(metric->texCoord);
			for (i32 ay = 0; ay < (i32)metric->atlasHeight; ++ay)
			{
				// Sample the high res field at the atlas pixel's center
				const real sy = (ay + 0.5f) * S - 0.5f;
				const i32 y0 = (i32)floorf(sy);
				const real ty = sy - y0;
				for (i32 ax = 0; ax < (i32)metric->atlasWidth; ++ax)
				{
					const real sx = (ax + 0.5f) * S - 0.5f;
					const i32 x0 = (i32)floorf(sx);
					const real tx = sx - x0;
					const real dist = glm::mix(
						glm::mix(SignedDist(x0, y0), SignedDist(x0 + 1, y0), tx),
						glm::mix(SignedDist(x0, y0 + 1), SignedDist(x0 + 1, y0 + 1), tx), ty);

					// 0.5 on the edge, increasing inwards, reaching 0/1 at spread atlas pixels from the edge
					const real value = glm::clamp(0.5f - (dist / S) / (2.0f * spreadAtlas), 0.0f, 1.0f);

					const i32 px = cellPos.x + ax;
					const i32 py = cellPos.y + ay;
					if (px < atlasSize.x && py < atlasSize.y)
					{
						outPixels[((size_t)py * atlasSize.x + px) * 4 + metric->channel] = (u8)(value * 255.0f + 0.5f);
					}
				}
			}
		});
		JobSystem::Wait(context);
	}

	const std::vector<flex::TextCache>& BitmapFont::GetTextCaches() const
	{
		return m_TextCaches;
	}

	void BitmapFont::AddTextCache(TextCache& newCache)
	{
		m_TextCaches.emplace_back(newCache);
	}

	void BitmapFont::ClearTexture()
	{
		m_Texture = nullptr;
	}

	glm::vec2u BitmapFont::GetTextureSize() const
	{
		return glm::vec2u(m_Texture->width, m_Texture->height);
	}

	u32 BitmapFont::GetTextureChannelCount() const
	{
		return m_Texture->channelCount;
	}

	Texture* BitmapFont::SetTexture(Texture* newTex)
	{
		m_Texture = newTex;
		return newTex;
	}

	Texture* BitmapFont::GetTexture()
	{
		return m_Texture;
	}

	void BitmapFont::ClearCaches()
	{
		m_TextCaches.clear();
	}

} // namespace flex