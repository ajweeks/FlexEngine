#include "stdafx.hpp"

#include "Graphics/BitmapFont.hpp"


namespace flex
{
	BitmapFont::BitmapFont(i32 size, const std::string& name, i32 charCount) :
		m_FontSize(size),
		m_Name(name),
		m_CharacterCount(charCount)
	{
		assert(size > 0);
		assert(charCount > 0);

		// TODO: Is this needed? (double check in release config)
		for (i32 i = 0; i < CHAR_COUNT; ++i)
		{
			m_CharTable[i].Kerning = std::map<wchar_t, glm::vec2>();
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

	glm::vec2 FontMetric::GetKerningOffset(wchar_t previous)
	{
		glm::vec2 kerningVec(0.0f);

		auto kerningIt = Kerning.find(previous);
		if (kerningIt != Kerning.end())
		{
			kerningVec = kerningIt->second;
		}

		return kerningVec;
	}

	TextCache::TextCache(const std::string& text, glm::vec2 pos, glm::vec4 col, i16 size) :
		Text(text), Position(pos), Color(col), Size(size)
	{
	}

	i16 BitmapFont::GetFontSize() const
	{
		return m_FontSize;
	}

	bool BitmapFont::GetUseKerning() const
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