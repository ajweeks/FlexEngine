#include "stdafx.hpp"

#include "Graphics/BitmapFont.hpp"

namespace flex
{
	BitmapFont::BitmapFont(i16 size, const std::string& name, i32 charCount) :
		m_FontSize(size),
		m_Name(name),
		m_CharacterCount(charCount)
	{
		assert(size > 0);
		assert(charCount > 0);

		// TODO: Is this needed? (double check in release config)
		for (i32 i = 0; i < CHAR_COUNT; ++i)
		{
			m_CharTable[i].kerning = std::map<std::string, glm::vec2>();
		}
	}

	BitmapFont::~BitmapFont()
	{
		SafeDelete(m_Texture);
	}

	bool BitmapFont::IsCharValid(wchar_t character)
	{
		return (character >= 0 && character < CHAR_COUNT);
	}
	
	void BitmapFont::SetMetric(const FontMetric& metric, wchar_t character)
	{
		m_CharTable[character] = metric;
	}
	
	FontMetric* BitmapFont::GetMetric(wchar_t character)
	{
		return &m_CharTable[character];
	}

	glm::vec2 FontMetric::GetKerningOffset(wchar_t leftChar, wchar_t rightChar)
	{
		glm::vec2 kerningVec(0.0f);

		std::string charKey(std::string(1, leftChar) + std::string(1, rightChar));

		auto kerningIt = kerning.find(charKey);
		if (kerningIt != kerning.end())
		{
			kerningVec = kerningIt->second;
		}

		return kerningVec;
	}

	TextCache::TextCache(const std::string& str, glm::vec2 pos, glm::vec4 color, real xSpacing, const std::vector<real>& letterYOffsets) :
		str(str),
		pos(pos),
		color(color),
		xSpacing(xSpacing),
		letterYOffsets(letterYOffsets)
	{
	}

	i16 BitmapFont::GetFontSize() const
	{
		return m_FontSize;
	}

	bool BitmapFont::UseKerning() const
	{
		return m_bUseKerning;
	}

	void BitmapFont::SetTextureSize(const glm::vec2i& texSize)
	{
		m_TextureWidth = texSize.x;
		m_TextureHeight = texSize.y;
	}

	gl::GLTexture* BitmapFont::SetTexture(gl::GLTexture* newTex)
	{
		m_Texture = newTex;
		return newTex;
	}

	gl::GLTexture* BitmapFont::GetTexture()
	{
		return m_Texture;
	}

} // namespace flex