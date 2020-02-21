#include "stdafx.hpp"

#if COMPILE_OPEN_GL
#include "Graphics/GL/GLHelpers.hpp"
#elif COMPILE_VULKAN
#include "Graphics/Vulkan/VulkanHelpers.hpp"
#endif

#include "Graphics/BitmapFont.hpp"

namespace flex
{
	BitmapFont::BitmapFont(const FontMetaData& inMetaData, const std::string& name, i32 charCount) :
		name(name),
		characterCount(charCount),
		metaData(inMetaData)
	{
		metaData.bitmapFont = this;
		assert(metaData.size > 0);
		assert(charCount > 0);
	}

	BitmapFont::~BitmapFont()
	{
		delete m_Texture;
		m_Texture = nullptr;
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

#if COMPILE_OPEN_GL
	gl::GLTexture* BitmapFont::SetTexture(gl::GLTexture* newTex)
	{
		m_Texture = newTex;
		return newTex;
	}

	gl::GLTexture* BitmapFont::GetTexture()
	{
		return m_Texture;
	}
#elif COMPILE_VULKAN
	vk::VulkanTexture* BitmapFont::SetTexture(vk::VulkanTexture* newTex)
	{
		m_Texture = newTex;
		return newTex;
	}

	vk::VulkanTexture* BitmapFont::GetTexture()
	{
		return m_Texture;
	}
#endif

	void BitmapFont::ClearCaches()
	{
		m_TextCaches.clear();
	}

} // namespace flex