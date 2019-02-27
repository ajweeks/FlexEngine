#pragma once

#include "Helpers.hpp"

namespace flex
{
#if COMPILE_OPEN_GL
	namespace gl
	{
		class GLRenderer;
		struct GLTexture;
	}
#elif COMPILE_VULKAN
	namespace vk
	{
		struct VulkanTexture;
	}
#endif

	struct FontMetric
	{
		glm::vec2 GetKerningOffset(wchar_t leftChar, wchar_t rightChar);

		bool bIsValid = false;
		wchar_t character = 0;

		u16 width = 0;
		u16 height = 0;
		i16 offsetX = 0;
		i16 offsetY = 0;

		real advanceX = 0;
		std::map<std::wstring, glm::vec2> kerning;

		//u8 page = 0;
		u8 channel = 0;
		glm::vec2 texCoord;
	};

	class BitmapFont
	{
	public:
		BitmapFont(i16 size, const std::string& name, i32 charCount);
		~BitmapFont();

		static bool IsCharValid(wchar_t character);

		FontMetric* GetMetric(wchar_t character);
		void SetMetric(const FontMetric& metric, wchar_t character);

		i16 GetSize() const;
		bool UseKerning() const;
		void SetUseKerning(bool bUseKerning);

		void SetTextureSize(const glm::vec2i& texSize);

		const std::vector<TextCache>& GetTextCaches() const;
		void AddTextCache(TextCache& newCache);
		void ClearCaches();

#if COMPILE_OPEN_GL
		gl::GLTexture* SetTexture(gl::GLTexture* newTex);
		gl::GLTexture* GetTexture();
#elif COMPILE_VULKAN
		vk::VulkanTexture* SetTexture(vk::VulkanTexture* newTex);
		vk::VulkanTexture* GetTexture();
#endif
		void ClearTexture();

		void SetBufferSize(i32 size);
		i32 GetBufferSize() const;
		void SetBufferStart(i32 start);
		i32 GetBufferStart() const;

		// TODO: Investigate crash when this value is higher (256)
		static const i32 CHAR_COUNT = 200;

		std::string name;

	private:
		FontMetric m_CharTable[CHAR_COUNT];
		std::vector<TextCache> m_TextCaches;

		i16 m_FontSize = 0;
		i32 m_CharacterCount = 0;
		i32 m_CharacterSpacing = 1;
		bool m_bUseKerning = false;
		i32 m_TextureWidth = 0;
		i32 m_TextureHeight = 0;
		i32 m_BufferStart = 0;
		i32 m_BufferSize = 0;
#if COMPILE_OPEN_GL
		gl::GLTexture* m_Texture = nullptr;
#elif COMPILE_VULKAN
		vk::VulkanTexture* m_Texture = nullptr;
#endif
		bool m_bAddedToRenderer = false;

		bool m_bIsCachedFont = false;

	};
} // namespace flex
