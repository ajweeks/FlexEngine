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

	// The game's visual design: colours, type sizes & spacing shared by every screen & the HUD. See docs/visual_design.md
	// for the roles & rules. Colours are written as sRGB hex (as displayed). Sizes are pixels at 1080p (multiply by
	// GameUI::Scale()).
	namespace UITheme
	{
		inline glm::vec4 Hex(u32 rgb, real alpha = 1.0f)
		{
			return glm::vec4((real)((rgb >> 16) & 0xFF) / 255.0f, (real)((rgb >> 8) & 0xFF) / 255.0f, (real)(rgb & 0xFF) / 255.0f, alpha);
		}
		inline glm::vec4 WithAlpha(const glm::vec4& colour, real alpha)
		{
			return glm::vec4(colour.r, colour.g, colour.b, colour.a * alpha);
		}

		// Surfaces: warm, slightly blue-green "night" neutrals (never pure black)
		const glm::vec4 INK = Hex(0x111318); // Scrims & backdrops (used translucent)
		const glm::vec4 SURFACE = Hex(0x1A1D23, 0.90f); // Panels
		const glm::vec4 SURFACE_TOP = Hex(0x23272E, 0.92f); // Top of a panel's gradient
		const glm::vec4 RAISED = Hex(0x2A2F37, 0.94f); // Widgets on a panel
		const glm::vec4 RAISED_HOVER = Hex(0x363C46, 0.96f);
		const glm::vec4 OUTLINE = Hex(0x48505C); // Tracks, dividers, hairlines

		// Text
		const glm::vec4 TEXT_PRIMARY = Hex(0xF3EFE7); // Warm white
		const glm::vec4 TEXT_SECONDARY = Hex(0xADA9A0); // Labels, values, hints
		const glm::vec4 TEXT_DISABLED = Hex(0x6E6B66);
		const glm::vec4 TEXT_ON_ACCENT = Hex(0x1C140B); // On ember fills

		// Accent: ember (the one colour that means "act here / you are here")
		const glm::vec4 EMBER = Hex(0xF4A23C);
		const glm::vec4 EMBER_DEEP = Hex(0xD9822B); // Resting primary buttons, gradients' lower half
		const glm::vec4 EMBER_GLOW = Hex(0xFFC56E); // Hovered primary buttons, highlights

		// Semantic
		const glm::vec4 SKY = Hex(0x6CB6D9); // Information, water, cool status
		const glm::vec4 MOSS = Hex(0x86C07A); // Success, confirmations
		const glm::vec4 CLAY = Hex(0xE0614F); // Danger, destructive actions, damage
		const glm::vec4 GOLD = Hex(0xF5CE5E); // Treasure & rewards

		// Older names, kept for existing code
		const glm::vec4 PANEL = SURFACE;
		const glm::vec4 PANEL_TOP = SURFACE_TOP;
		const glm::vec4 WIDGET = RAISED;
		const glm::vec4 WIDGET_HOVER = RAISED_HOVER;
		const glm::vec4 ACCENT = EMBER;
		const glm::vec4 ACCENT_DARK = EMBER_DEEP;
		const glm::vec4 DANGER = CLAY;
		const glm::vec4 TEXT_MAIN = TEXT_PRIMARY;
		const glm::vec4 TEXT_DIM = TEXT_SECONDARY;
		const glm::vec4 TEXT_DARK = TEXT_ON_ACCENT;

		// Type scale (cap height in pixels)
		const real TEXT_DISPLAY = 80.0f; // Screen titles (heading font)
		const real TEXT_H1 = 44.0f; // Dialog titles (heading font)
		const real TEXT_H2 = 32.0f; // Section titles (heading font)
		const real TEXT_BODY_LARGE = 20.0f; // Buttons, list titles
		const real TEXT_BODY = 18.0f; // Widget labels & values
		const real TEXT_SMALL = 16.0f; // Hints, subtitles
		const real TEXT_CAPTION = 14.0f; // Metadata, key legends

		// Spacing (a 4px grid)
		const real SPACE_XS = 4.0f;
		const real SPACE_S = 8.0f;
		const real SPACE_M = 12.0f;
		const real SPACE_L = 16.0f;
		const real SPACE_XL = 24.0f;
		const real SPACE_XXL = 32.0f;
		const real SCREEN_MARGIN = 120.0f; // Left edge of menu content
		const real HUD_MARGIN = 32.0f; // Distance of HUD elements from the screen's edges

		// Corner radii
		const real RADIUS_S = 4.0f; // Bars, markers
		const real RADIUS_M = 10.0f; // Widgets
		const real RADIUS_L = 16.0f; // Panels, HUD plates

		const real CORNER_RADIUS = RADIUS_M;
		const real BUTTON_HEIGHT = 56.0f;
		const real ROW_HEIGHT = 62.0f; // Settings rows, text fields
		const real SPACING = SPACE_M;
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
		void FillRectGradientHorizontal(const UIRect& rect, const glm::vec4& left, const glm::vec4& right, real cornerRadius = 0.0f);
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
		// Returns true while the value is being changed (see WasReleased for when dragging ends)
		bool Slider(const char* id, const UIRect& rect, const std::string& label, real* value, real min, real max, const char* format);
		// Returns true when enter is pressed
		bool TextField(const char* id, const UIRect& rect, std::string* text, u32 maxLength, const std::string& placeholder = "");
		// Gives the text field with this id the keyboard
		void Focus(const char* id);
		// Selectable row in a list
		bool ListItem(const char* id, const UIRect& rect, const std::string& title, const std::string& subtitle, bool bSelected);
		// Vertical scrollbar for a list showing viewSize of contentSize (in any unit, e.g. rows). *scroll (0 to
		// contentSize - viewSize) is changed by dragging the thumb or clicking the track. Returns true while dragging.
		bool Scrollbar(const char* id, const UIRect& rect, real contentSize, real viewSize, real* scroll);

		// A small label next to the mouse (drawn over whatever's under it), for the thing being hovered
		void Tooltip(const std::string& text);

		void ClearFocus() { m_FocusedID = 0; }
		// True on the frame the mouse is released after dragging the widget (e.g. a slider)
		bool WasReleased(const char* id) const;
		// True while a widget is being dragged
		bool IsDragging() const { return m_ActiveID != 0; }

		// Short, soft tone confirming a change (e.g. played when the volume slider is released, at the new volume)
		void PlayConfirm();

		// Draws a rounded rect without a GameUI instance (e.g. from the inventory UI), in pixels from the top left
		static void DrawRoundedRect(const UIRect& rect, const glm::vec4& top, const glm::vec4& bottom, real cornerRadius);

	private:
		// Smoothly animated hover amount for a widget
		real HoverAmount(u64 id, bool bHovered);
		void PlayClick();
		void FillRectGradientInternal(const UIRect& rect, const glm::vec4& from, const glm::vec4& to, real cornerRadius, bool bHorizontal);

		real m_Width = 1.0f;
		real m_Height = 1.0f;
		real m_Scale = 1.0f;
		glm::vec2 m_MousePos = glm::vec2(-1.0f);
		bool m_bMousePressed = false;
		bool m_bMouseDown = false;
		real m_Scroll = 0.0f;

		u64 m_ActiveID = 0; // Widget being dragged
		real m_DragOffset = 0.0f; // From the dragged widget's handle to the mouse
		u64 m_ReleasedID = 0; // Widget whose drag ended this frame
		u64 m_FocusedID = 0; // Text field receiving keyboard input
		std::map<u64, real> m_HoverAmounts;
		sec m_Time = 0.0f;
		AudioSourceID m_ClickSoundID = InvalidAudioSourceID;
		AudioSourceID m_ConfirmSoundID = InvalidAudioSourceID;
	};
} // namespace flex
