#pragma once

#include "Helpers.hpp"

#if COMPILE_VULKAN
IGNORE_WARNINGS_PUSH
#include "volk/volk.h"
IGNORE_WARNINGS_POP
#endif

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

	struct FontMetaData
	{
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

		FontMetric* GetMetric(wchar_t character);
		void SetMetric(const FontMetric& metric, wchar_t character);

		const std::vector<TextCache>& GetTextCaches() const;
		void AddTextCache(TextCache& newCache);
		void ClearCaches();

#if COMPILE_OPEN_GL
		gl::GLTexture* SetTexture(gl::GLTexture* newTex);
		gl::GLTexture* GetTexture();
#elif COMPILE_VULKAN
		vk::VulkanTexture* SetTexture(vk::VulkanTexture* newTex);
		vk::VulkanTexture* GetTexture();
		// TODO: Remove all platform specific code from file
		VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
#endif
		void ClearTexture();

		glm::vec2u GetTextureSize() const;
		u32 GetTextureChannelCount() const;

		// TODO: Investigate crash when this value is higher (256)
		static const i32 CHAR_COUNT = 200;

		std::string name;
		i32 characterCount = 0;
		bool bUseKerning = false;
		i32 bufferStart = 0;
		i32 bufferSize = 0;

		FontMetaData metaData;

	private:
		FontMetric m_CharTable[CHAR_COUNT];
		std::vector<TextCache> m_TextCaches;

#if COMPILE_OPEN_GL
		gl::GLTexture* m_Texture = nullptr;
#elif COMPILE_VULKAN
		vk::VulkanTexture* m_Texture = nullptr;
#endif

	};
} // namespace flex
