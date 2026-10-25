#pragma once

// Immediate mode UI for the game's menus & screens (main menu, settings, pause menu, map). Everything is laid out in
// pixels from the top left of the window, scaled with the window's height so it looks the same at any resolution.
namespace flex
{
	struct UIRect
	{
		real x = 0.0f;
		real y = 0.0f;
		real w = 0.0f;
		real h = 0.0f;

		UIRect() = default;
		UIRect(real x, real y, real w, real h) : x(x), y(y), w(w), h(h) {}

		bool Contains(const glm::vec2& p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
		glm::vec2 Center() const { return glm::vec2(x + w * 0.5f, y + h * 0.5f); }
		UIRect Inset(real amount) const { return UIRect(x + amount, y + amount, w - amount * 2.0f, h - amount * 2.0f); }
		// Splits off the top `height` pixels, leaving the rest
		UIRect CutTop(real height, real gap = 0.0f);
	};

	enum class TextAlign
	{
		LEFT,
		CENTER,
		RIGHT
	};

	// Colours & sizes shared by all screens
	namespace UITheme
	{
		const glm::vec4 PANEL(0.06f, 0.07f, 0.09f, 0.86f);
		const glm::vec4 PANEL_TOP(0.11f, 0.12f, 0.15f, 0.9f);
		const glm::vec4 WIDGET(0.16f, 0.17f, 0.21f, 0.92f);
		const glm::vec4 WIDGET_HOVER(0.24f, 0.26f, 0.32f, 0.95f);
		const glm::vec4 ACCENT(0.98f, 0.66f, 0.24f, 1.0f);
		const glm::vec4 ACCENT_DARK(0.78f, 0.45f, 0.12f, 1.0f);
		const glm::vec4 DANGER(0.82f, 0.28f, 0.24f, 1.0f);
		const glm::vec4 TEXT_MAIN(0.95f, 0.95f, 0.96f, 1.0f);
		const glm::vec4 TEXT_DIM(0.65f, 0.67f, 0.72f, 1.0f);
		const glm::vec4 TEXT_DARK(0.08f, 0.08f, 0.1f, 1.0f);
		const real CORNER_RADIUS = 10.0f;
		const real BUTTON_HEIGHT = 56.0f;
		const real SPACING = 12.0f;
	}

	class GameUI final
	{
	public:
		// Reads input & sizes for this frame
		void BeginFrame();

		real Scale() const { return m_Scale; }
		real Width() const { return m_Width; }
		real Height() const { return m_Height; }
		glm::vec2 MousePos() const { return m_MousePos; }
		bool IsMouseOver(const UIRect& rect) const { return rect.Contains(m_MousePos); }
		bool WasClicked(const UIRect& rect) const { return m_bMousePressed && rect.Contains(m_MousePos); }
		bool IsMousePressed() const { return m_bMousePressed; }
		real ScrollDelta() const { return m_Scroll; }

		// Drawing
		void FillRect(const UIRect& rect, const glm::vec4& colour, real cornerRadius = 0.0f);
		void FillRectGradient(const UIRect& rect, const glm::vec4& top, const glm::vec4& bottom, real cornerRadius = 0.0f);
		// Soft drop shadow behind a rect
		void Shadow(const UIRect& rect, real blur, real cornerRadius, real opacity = 0.45f);
		void Panel(const UIRect& rect);
		void Circle(const glm::vec2& center, real radius, const glm::vec4& colour);
		void Line(const glm::vec2& a, const glm::vec2& b, real thickness, const glm::vec4& colour);

		// Text is drawn vertically centered on y
		void Text(const std::string& text, real x, real y, real size, const glm::vec4& colour, TextAlign align = TextAlign::LEFT, bool bHeading = false);
		real MeasureText(const std::string& text, real size, bool bHeading = false);

		// Widgets, identified by their id (which must be unique on the screen)
		bool Button(const char* id, const UIRect& rect, const std::string& label, bool bPrimary = false, bool bEnabled = true);
		bool Checkbox(const char* id, const UIRect& rect, const std::string& label, bool* value);
		// Returns true while the value is being changed
		bool Slider(const char* id, const UIRect& rect, const std::string& label, real* value, real min, real max, const char* format);
		// Returns true when enter is pressed
		bool TextField(const char* id, const UIRect& rect, std::string* text, u32 maxLength, const std::string& placeholder = "");
		// Selectable row in a list
		bool ListItem(const char* id, const UIRect& rect, const std::string& title, const std::string& subtitle, bool bSelected);

		void ClearFocus() { m_FocusedID = 0; }

		// Draws a rounded rect without a GameUI instance (e.g. from the inventory UI), in pixels from the top left
		static void DrawRoundedRect(const UIRect& rect, const glm::vec4& top, const glm::vec4& bottom, real cornerRadius);

	private:
		// Smoothly animated hover amount for a widget
		real HoverAmount(u64 id, bool bHovered);
		void PlayClick();

		real m_Width = 1.0f;
		real m_Height = 1.0f;
		real m_Scale = 1.0f;
		glm::vec2 m_MousePos = glm::vec2(-1.0f);
		bool m_bMousePressed = false;
		bool m_bMouseDown = false;
		real m_Scroll = 0.0f;

		u64 m_ActiveID = 0; // Widget being dragged
		u64 m_FocusedID = 0; // Text field receiving keyboard input
		std::map<u64, real> m_HoverAmounts;
		sec m_Time = 0.0f;
		AudioSourceID m_ClickSoundID = InvalidAudioSourceID;
	};
} // namespace flex
