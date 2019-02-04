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
		//for (i32 i = 0; i < CHAR_COUNT; ++i)
		//{
		//	m_CharTable[i].kerning = std::map<std::wstring, glm::vec2>();
		//}
	}

	BitmapFont::~BitmapFont()
	{
		if (m_Texture)
		{
			m_Texture->Destroy();
			SafeDelete(m_Texture);
		}
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

		std::wstring charKey(std::wstring(1, leftChar) + std::wstring(1, rightChar));

		auto iter = kerning.find(charKey);
		if (iter != kerning.end())
		{
			kerningVec = iter->second;
		}

		return kerningVec;
	}

	i16 BitmapFont::GetSize() const
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

		if (m_Texture)
		{
			m_Texture->width = m_TextureWidth;
			m_Texture->height = m_TextureHeight;
		}
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