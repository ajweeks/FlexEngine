#pragma once

namespace flex
{
	namespace gl
	{
		struct GLTexture;
	}

	struct FontMetric
	{
		glm::vec2 GetKerningOffset(wchar_t previous);

		bool IsValid = false;
		wchar_t Character = 0;

		u16 Width = 0;
		u16 Height = 0;
		i16 OffsetX = 0;
		i16 OffsetY = 0;

		float AdvanceX = 0;
		std::map<wchar_t, glm::vec2> Kerning;

		u8 Page = 0;
		u8 Channel = 0;
		glm::vec2 TexCoord;
	};

	struct TextCache
	{
	public:
		TextCache(const std::string& text, glm::vec2 pos, glm::vec4 col, i16 size);

		const std::string Text;
		glm::vec2 Position;
		glm::vec4 Color;
		i16 Size;

	private:
		//TextCache& operator=(const TextCache &tmp);

	};

	class BitmapFont
	{
	public:
		BitmapFont(i32 size, const std::string& name, i32 charCount);
		~BitmapFont();

		static bool IsCharValid(wchar_t character);

		FontMetric* GetMetric(wchar_t character);
		void SetMetric(const FontMetric& metric, wchar_t character);

		i16 GetFontSize() const;
		bool GetUseKerning() const;

		void SetTextureSize(const glm::vec2i& texSize);

		gl::GLTexture* SetTexture(gl::GLTexture* newTex);
		gl::GLTexture* GetTexture();

		// TODO: Investigate crash when this value is higher (256)
		static const i32 CHAR_COUNT = 200;

	private:
		FontMetric m_CharTable[CHAR_COUNT];
		std::vector<TextCache> m_TextCache;

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
		bool m_bIsAddedToRenderer = false;

		bool m_bIsCachedFont = false;

	};
} // namespace flex
