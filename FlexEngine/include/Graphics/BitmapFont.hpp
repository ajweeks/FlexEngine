#pragma once

#include "Helpers.hpp"

struct FT_FaceRec_;

namespace flex
{
	struct Texture;

	// Glyph metrics are in "layout units": pixels at the font's nominal size (size / 2 points at the monitor's DPI).
	// The SDF atlas holds glyphs at BitmapFont::atlasScale atlas pixels per layout unit.
	struct FontMetric
	{
		bool bIsValid = false;
		wchar_t character = 0;

		// Size & offset (from the pen position on the baseline) of the glyph quad, including padding
		real width = 0;
		real height = 0;
		real offsetX = 0;
		real offsetY = 0;

		// Unhinted advance
		real advanceX = 0;

		// Size of the glyph's cell in the atlas (in atlas pixels)
		u16 atlasWidth = 0;
		u16 atlasHeight = 0;

		//u8 page = 0;
		u8 channel = 0;
		glm::vec2 texCoord;
	};

	struct FontMetaData
	{
		std::string name;
		std::string filePath;
		std::string renderedTextureFilePath;
		i16 size = 0;
		bool bScreenSpace = true;
		real threshold = 0.5f;
		glm::vec2 shadowOffset;
		real shadowOpacity = 0.5f;
		real soften = 0.035f;
		bool bDirty = false;
		class BitmapFont* bitmapFont = nullptr;
	};

	class BitmapFont
	{
	public:
		BitmapFont(const FontMetaData& inMetaData, const std::string& name, i32 charCount);
		~BitmapFont();

		static bool IsCharValid(wchar_t character);

		// Horizontal kerning (in layout units) to add between leftChar & rightChar
		real GetKerning(wchar_t leftChar, wchar_t rightChar) const;
		void SetKerning(wchar_t leftChar, wchar_t rightChar, real kerning);

		// Extra space (in layout units) to add after each glyph when drawn at pixelsPerUnit screen pixels per layout
		// unit. Anti-aliasing blurs the narrow gaps between small glyphs together, so small text is tracked out a little.
		real GetTracking(real pixelsPerUnit) const;
		// Reads kerning between all pairs of characters from the font file
		void LoadKerning(const std::vector<char>& fileMemory, real fontUnitsToLayout, const std::map<i32, FontMetric*>& characters);

		// Renders a signed distance field of each glyph in characters into their cells of an RGBA atlas
		static void GenerateSDFAtlas(::FT_FaceRec_* face, BitmapFont* font, const std::map<i32, FontMetric*>& characters,
			const glm::vec2i& atlasSize, std::vector<u8>& outPixels);

		FontMetric* GetMetric(wchar_t character);
		void SetMetric(const FontMetric& metric, wchar_t character);

		const std::vector<TextCache>& GetTextCaches() const;
		void AddTextCache(TextCache& newCache);
		void ClearCaches();

		Texture* SetTexture(Texture* newTex);
		Texture* GetTexture();

		// Graphics API-defined data (descriptor set in Vulkan)
		u64 userData = 0;

		void ClearTexture();

		glm::vec2u GetTextureSize() const;
		u32 GetTextureChannelCount() const;

		// TODO: Investigate crash when this value is higher (256)
		static const i32 CHAR_COUNT = 200;

		std::string name;
		i32 characterCount = 0;
		bool bUseKerning = false;
		// Atlas pixels per layout unit (rendering small fonts larger keeps thin features in the SDF)
		u32 atlasScale = 1;
		// Em size in layout units
		real emSize = 0.0f;
		// Supersampling of the coverage the SDF is computed from (per atlas pixel)
		static const u32 SDF_SUPERSAMPLE = 4;
		// Distance (in layout units) the SDF encodes to either side of the edge, and the empty border around it
		static const u32 SDF_SPREAD = 5;
		static const u32 SDF_PADDING = 1;
		// Bump when the atlas generation changes to invalidate atlases cached in saved/fonts
		static const u32 SDF_ATLAS_VERSION = 2;
		i32 bufferStart = 0;
		i32 bufferSize = 0;

		FontMetaData metaData;

	private:
		FontMetric m_CharTable[CHAR_COUNT];
		// (leftChar << 16 | rightChar) -> kerning
		std::unordered_map<u32, real> m_Kerning;
		std::vector<TextCache> m_TextCaches;

		Texture* m_Texture = nullptr;
	};
} // namespace flex
