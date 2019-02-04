#pragma once

#include "Helpers.hpp"
#// TODO: FIXME: GROSS: Remove!!! Ahhhh
#include "Graphics/GL/GLRenderer.hpp"

namespace flex
{
	namespace gl
	{
		struct GLTexture;
	}

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

		void SetTextureSize(const glm::vec2i& texSize);

		gl::GLTexture* SetTexture(gl::GLTexture* newTex);
		gl::GLTexture* GetTexture();

		// TODO: Investigate crash when this value is higher (256)
		static const i32 CHAR_COUNT = 200;

	private:
		// TODO: Remove
		friend class flex::gl::GLRenderer;

		FontMetric m_CharTable[CHAR_COUNT];
		std::vector<TextCache> m_TextCaches;

		i16 m_FontSize = 0;
		std::string m_Name;
		i32 m_CharacterCount = 0;
		i32 m_CharacterSpacing = 1;
		bool m_bUseKerning = false;
		i32 m_TextureWidth = 0;
		i32 m_TextureHeight = 0;
		i32 m_BufferStart = 0;
		i32 m_BufferSize = 0;
		gl::GLTexture* m_Texture = nullptr;
		bool m_bAddedToRenderer = false;

		bool m_bIsCachedFont = false;

	};
} // namespace flex
