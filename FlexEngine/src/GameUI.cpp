#include "stdafx.hpp"

#include "GameUI.hpp"

#include "Audio/AudioManager.hpp"
#include "Graphics/BitmapFont.hpp"
#include "Graphics/Renderer.hpp"
#include "InputManager.hpp"
#include "ResourceManager.hpp"
#include "UIMesh.hpp"
#include "Window/Window.hpp"

namespace flex
{
	namespace
	{
		u64 HashID(const char* id)
		{
			// FNV-1a
			u64 hash = 14695981039346656037ull;
			for (const char* c = id; *c != 0; ++c)
			{
				hash ^= (u64)(u8)*c;
				hash *= 1099511628211ull;
			}
			return hash;
		}

		const u32 CORNER_SEGMENTS = 6;
	} // anonymous namespace

	UIRect UIRect::CutTop(real height, real gap /* = 0.0f */)
	{
		UIRect result(x, y, w, height);
		y += height + gap;
		h -= height + gap;
		return result;
	}

	void GameUI::BeginFrame()
	{
		const glm::vec2i size = g_Window->GetSize();
		m_Width = (real)glm::max(size.x, 1);
		m_Height = (real)glm::max(size.y, 1);
		m_Scale = m_Height / 1080.0f;
		m_MousePos = g_InputManager->GetMousePosition();
		m_bMousePressed = g_InputManager->IsMouseButtonPressed(MouseButton::LEFT);
		m_bMouseDown = g_InputManager->IsMouseButtonDown(MouseButton::LEFT);
		m_Scroll = g_InputManager->GetVerticalScrollDistance();
		m_Time += g_UnpausedDeltaTime;
		if (!m_bMouseDown)
		{
			m_ActiveID = 0;
		}
		if (m_ClickSoundID == InvalidAudioSourceID)
		{
			m_ClickSoundID = g_ResourceManager->GetOrLoadAudioSourceID(SID("click-02.wav"), true);
		}
	}

	void GameUI::PlayClick()
	{
		if (m_ClickSoundID != InvalidAudioSourceID)
		{
			AudioManager::PlaySourceWithGain(m_ClickSoundID, 0.4f);
		}
	}

	real GameUI::HoverAmount(u64 id, bool bHovered)
	{
		real& amount = m_HoverAmounts[id];
		const real target = bHovered ? 1.0f : 0.0f;
		amount = glm::mix(amount, target, glm::clamp(g_UnpausedDeltaTime * 14.0f, 0.0f, 1.0f));
		return amount;
	}

	// Rounded rects are triangle fans with an extra ring of transparent vertices around their edge, which anti-aliases
	// it (& with a large ring, makes soft shadows)
	void GameUI::FillRectGradient(const UIRect& rect, const glm::vec4& top, const glm::vec4& bottom, real cornerRadius /* = 0.0f */)
	{
		if (rect.w <= 0.0f || rect.h <= 0.0f)
		{
			return;
		}

		const real feather = 1.0f;
		// At least the feather's width, so the inner edge's corners stay convex (concave ones flip triangles of the fan,
		// which then overlap & show as lines)
		const real radius = glm::clamp(cornerRadius, feather, glm::max(glm::min(rect.w, rect.h) * 0.5f, feather));

		std::vector<glm::vec2> outline;
		outline.reserve(4 * (CORNER_SEGMENTS + 1));
		const glm::vec2 cornerCenters[4] =
		{
			glm::vec2(rect.x + rect.w - radius, rect.y + radius), // Top right
			glm::vec2(rect.x + radius, rect.y + radius), // Top left
			glm::vec2(rect.x + radius, rect.y + rect.h - radius), // Bottom left
			glm::vec2(rect.x + rect.w - radius, rect.y + rect.h - radius), // Bottom right
		};
		std::vector<glm::vec2> normals;
		for (u32 corner = 0; corner < 4; ++corner)
		{
			for (u32 i = 0; i <= CORNER_SEGMENTS; ++i)
			{
				// Angles in screen space (+y down): top right corner spans -90..0 degrees
				const real angle = -PI_DIV_TWO * (real)corner - PI_DIV_TWO * (real)i / (real)CORNER_SEGMENTS;
				const glm::vec2 normal(std::cos(angle), std::sin(angle));
				outline.push_back(cornerCenters[corner] + normal * radius);
				normals.push_back(normal);
			}
		}

		std::vector<glm::vec2> points;
		std::vector<glm::vec4> colours;
		std::vector<u32> indices;
		const u32 outlineCount = (u32)outline.size();

		auto ToNDC = [this](const glm::vec2& p) { return glm::vec2(p.x / m_Width * 2.0f - 1.0f, 1.0f - p.y / m_Height * 2.0f); };
		auto ColourAt = [&](real y, real alphaScale)
		{
			const real t = glm::clamp((y - rect.y) / rect.h, 0.0f, 1.0f);
			glm::vec4 c = glm::mix(top, bottom, t);
			c.a *= alphaScale;
			return c;
		};

		// Center, inner ring, outer (transparent) ring
		points.push_back(ToNDC(rect.Center()));
		colours.push_back(ColourAt(rect.Center().y, 1.0f));
		for (u32 i = 0; i < outlineCount; ++i)
		{
			const glm::vec2 inner = outline[i] - normals[i] * feather * 0.5f;
			points.push_back(ToNDC(inner));
			colours.push_back(ColourAt(inner.y, 1.0f));
		}
		for (u32 i = 0; i < outlineCount; ++i)
		{
			const glm::vec2 outer = outline[i] + normals[i] * feather * 0.5f;
			points.push_back(ToNDC(outer));
			colours.push_back(ColourAt(outer.y, 0.0f));
		}

		for (u32 i = 0; i < outlineCount; ++i)
		{
			const u32 i1 = (i + 1) % outlineCount;
			const u32 inner0 = 1 + i;
			const u32 inner1 = 1 + i1;
			const u32 outer0 = 1 + outlineCount + i;
			const u32 outer1 = 1 + outlineCount + i1;
			indices.insert(indices.end(), { 0, inner1, inner0 });
			indices.insert(indices.end(), { inner0, inner1, outer1 });
			indices.insert(indices.end(), { inner0, outer1, outer0 });
		}

		g_Renderer->GetUIMesh()->DrawPolygonColoured(points, colours, indices);
	}

	void GameUI::DrawRoundedRect(const UIRect& rect, const glm::vec4& top, const glm::vec4& bottom, real cornerRadius)
	{
		GameUI ui;
		const glm::vec2i size = g_Window->GetSize();
		ui.m_Width = (real)glm::max(size.x, 1);
		ui.m_Height = (real)glm::max(size.y, 1);
		ui.m_Scale = ui.m_Height / 1080.0f;
		ui.FillRectGradient(rect, top, bottom, cornerRadius);
	}

	void GameUI::FillRect(const UIRect& rect, const glm::vec4& colour, real cornerRadius /* = 0.0f */)
	{
		FillRectGradient(rect, colour, colour, cornerRadius);
	}

	void GameUI::Shadow(const UIRect& rect, real blur, real cornerRadius, real opacity /* = 0.45f */)
	{
		// A rect whose alpha fades out over `blur` pixels: the inner ring is opaque & the outer is fully transparent
		const real radius = cornerRadius + blur;
		const UIRect outer(rect.x - blur, rect.y - blur + blur * 0.35f, rect.w + blur * 2.0f, rect.h + blur * 2.0f);

		std::vector<glm::vec2> points;
		std::vector<glm::vec4> colours;
		std::vector<u32> indices;
		auto ToNDC = [this](const glm::vec2& p) { return glm::vec2(p.x / m_Width * 2.0f - 1.0f, 1.0f - p.y / m_Height * 2.0f); };

		const glm::vec2 cornerCenters[4] =
		{
			glm::vec2(outer.x + outer.w - radius, outer.y + radius),
			glm::vec2(outer.x + radius, outer.y + radius),
			glm::vec2(outer.x + radius, outer.y + outer.h - radius),
			glm::vec2(outer.x + outer.w - radius, outer.y + outer.h - radius),
		};
		const glm::vec4 opaque(0.0f, 0.0f, 0.0f, opacity);
		const glm::vec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
		points.push_back(ToNDC(outer.Center()));
		colours.push_back(opaque);
		u32 count = 0;
		for (u32 corner = 0; corner < 4; ++corner)
		{
			for (u32 i = 0; i <= CORNER_SEGMENTS; ++i)
			{
				const real angle = -PI_DIV_TWO * (real)corner - PI_DIV_TWO * (real)i / (real)CORNER_SEGMENTS;
				const glm::vec2 normal(std::cos(angle), std::sin(angle));
				points.push_back(ToNDC(cornerCenters[corner] + normal * glm::max(radius - blur * 2.0f, 0.0f)));
				colours.push_back(opaque);
				points.push_back(ToNDC(cornerCenters[corner] + normal * radius));
				colours.push_back(clear);
				++count;
			}
		}
		for (u32 i = 0; i < count; ++i)
		{
			const u32 i1 = (i + 1) % count;
			const u32 inner0 = 1 + i * 2;
			const u32 outer0 = 2 + i * 2;
			const u32 inner1 = 1 + i1 * 2;
			const u32 outer1 = 2 + i1 * 2;
			indices.insert(indices.end(), { 0, inner1, inner0 });
			indices.insert(indices.end(), { inner0, inner1, outer1 });
			indices.insert(indices.end(), { inner0, outer1, outer0 });
		}
		g_Renderer->GetUIMesh()->DrawPolygonColoured(points, colours, indices);
	}

	void GameUI::Panel(const UIRect& rect)
	{
		const real radius = UITheme::CORNER_RADIUS * 1.5f * m_Scale;
		Shadow(rect, 24.0f * m_Scale, radius);
		FillRectGradient(rect, UITheme::PANEL_TOP, UITheme::PANEL, radius);
	}

	void GameUI::Circle(const glm::vec2& center, real radius, const glm::vec4& colour)
	{
		FillRect(UIRect(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f), colour, radius);
	}

	void GameUI::Line(const glm::vec2& a, const glm::vec2& b, real thickness, const glm::vec4& colour)
	{
		const glm::vec2 dir = b - a;
		const real length = glm::length(dir);
		if (length < 1e-3f)
		{
			return;
		}
		const glm::vec2 side = glm::vec2(-dir.y, dir.x) / length * thickness * 0.5f;
		auto ToNDC = [this](const glm::vec2& p) { return glm::vec2(p.x / m_Width * 2.0f - 1.0f, 1.0f - p.y / m_Height * 2.0f); };
		const std::vector<glm::vec2> points = { ToNDC(a + side), ToNDC(b + side), ToNDC(b - side), ToNDC(a - side) };
		const std::vector<glm::vec4> colours(4, colour);
		const std::vector<u32> indices = { 0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2 };
		g_Renderer->GetUIMesh()->DrawPolygonColoured(points, colours, indices);
	}

	// Screen space text is laid out in units where the window is 2 high (& 2 * aspect wide), with glyph quads hanging
	// down from the baseline position. Metrics are in the font atlas' pixels, scaled by size / 12.
	real GameUI::MeasureText(const std::string& text, real size, bool bHeading /* = false */)
	{
		BitmapFont* font = g_Renderer->SetFont(SID(bHeading ? "ui-heading" : "ui"));
		if (font == nullptr)
		{
			return 0.0f;
		}
		const FontMetric* capMetric = font->GetMetric('H');
		const real frameBufferScale = glm::max(2.0f / m_Width, 2.0f / m_Height);
		const real unitScale = frameBufferScale * (font->metaData.size / 12.0f);
		const real scale = size / glm::max(capMetric->height * unitScale * m_Height * 0.5f, 1e-4f);

		real advance = 0.0f;
		for (char c : text)
		{
			if (BitmapFont::IsCharValid(c))
			{
				advance += font->GetMetric(c)->advanceX;
			}
		}
		return advance * unitScale * scale * m_Height * 0.5f;
	}

	void GameUI::Text(const std::string& text, real x, real y, real size, const glm::vec4& colour, TextAlign align /* = TextAlign::LEFT */, bool bHeading /* = false */)
	{
		if (text.empty())
		{
			return;
		}

		BitmapFont* font = g_Renderer->SetFont(SID(bHeading ? "ui-heading" : "ui"));
		if (font == nullptr)
		{
			return;
		}
		const FontMetric* capMetric = font->GetMetric('H');
		const real frameBufferScale = glm::max(2.0f / m_Width, 2.0f / m_Height);
		const real unitScale = frameBufferScale * (font->metaData.size / 12.0f);
		// Size is the height of capital letters (in pixels)
		const real scale = size / glm::max(capMetric->height * unitScale * m_Height * 0.5f, 1e-4f);
		const real textScale = unitScale * scale;

		const real width = MeasureText(text, size, bHeading);
		real left = x;
		if (align == TextAlign::CENTER)
		{
			left = x - width * 0.5f;
		}
		else if (align == TextAlign::RIGHT)
		{
			left = x - width;
		}

		// Place the baseline so the capitals' quad is centered on y
		const real centerY = 1.0f - y / m_Height * 2.0f;
		const real capTop = (real)-capMetric->offsetY * textScale;
		const real baseline = centerY - (capTop - capMetric->height * textScale * 0.5f);
		const glm::vec2 pos(left / m_Width * 2.0f, baseline);
		g_Renderer->DrawStringSS(text, colour, AnchorPoint::LEFT, pos, 0.0f, scale);
	}

	bool GameUI::Button(const char* id, const UIRect& rect, const std::string& label, bool bPrimary /* = false */, bool bEnabled /* = true */)
	{
		const u64 hash = HashID(id);
		const bool bHovered = bEnabled && IsMouseOver(rect);
		const real hover = HoverAmount(hash, bHovered);
		const real radius = UITheme::CORNER_RADIUS * m_Scale;

		glm::vec4 base = bPrimary ? UITheme::ACCENT_DARK : UITheme::WIDGET;
		glm::vec4 hovered = bPrimary ? UITheme::ACCENT : UITheme::WIDGET_HOVER;
		if (!bEnabled)
		{
			base.a *= 0.4f;
			hovered = base;
		}
		const glm::vec4 colour = glm::mix(base, hovered, hover);
		// Grows slightly when hovered
		const real grow = hover * 3.0f * m_Scale;
		const UIRect drawn(rect.x - grow, rect.y - grow, rect.w + grow * 2.0f, rect.h + grow * 2.0f);
		Shadow(drawn, 8.0f * m_Scale, radius, 0.3f);
		FillRectGradient(drawn, colour * glm::vec4(1.12f, 1.12f, 1.12f, 1.0f), colour, radius);
		// Accent bar slides in on hover
		if (!bPrimary && hover > 0.01f)
		{
			FillRect(UIRect(drawn.x, drawn.y + drawn.h * 0.2f, 4.0f * m_Scale * hover, drawn.h * 0.6f), UITheme::ACCENT, 2.0f * m_Scale);
		}

		const glm::vec4 textColour = bEnabled ? (bPrimary ? UITheme::TEXT_DARK : UITheme::TEXT_MAIN) : UITheme::TEXT_DIM;
		Text(label, rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, 20.0f * m_Scale, textColour, TextAlign::CENTER);

		const bool bClicked = bEnabled && WasClicked(rect);
		if (bClicked)
		{
			PlayClick();
		}
		return bClicked;
	}

	bool GameUI::Checkbox(const char* id, const UIRect& rect, const std::string& label, bool* value)
	{
		const u64 hash = HashID(id);
		const bool bHovered = IsMouseOver(rect);
		const real hover = HoverAmount(hash, bHovered);
		const real radius = UITheme::CORNER_RADIUS * m_Scale;
		FillRect(rect, glm::mix(UITheme::WIDGET, UITheme::WIDGET_HOVER, hover), radius);
		Text(label, rect.x + 20.0f * m_Scale, rect.y + rect.h * 0.5f, 18.0f * m_Scale, UITheme::TEXT_MAIN);

		// Toggle switch on the right
		const real switchW = 56.0f * m_Scale;
		const real switchH = 28.0f * m_Scale;
		const UIRect track(rect.x + rect.w - switchW - 18.0f * m_Scale, rect.y + (rect.h - switchH) * 0.5f, switchW, switchH);
		const real on = HoverAmount(hash ^ 0x5bd1e995u, *value);
		FillRect(track, glm::mix(glm::vec4(0.3f, 0.31f, 0.35f, 1.0f), UITheme::ACCENT, on), switchH * 0.5f);
		const real knobRadius = switchH * 0.5f - 3.0f * m_Scale;
		Circle(glm::vec2(track.x + switchH * 0.5f + (switchW - switchH) * on, track.y + switchH * 0.5f), knobRadius, UITheme::TEXT_MAIN);

		if (WasClicked(rect))
		{
			*value = !*value;
			PlayClick();
			return true;
		}
		return false;
	}

	bool GameUI::Slider(const char* id, const UIRect& rect, const std::string& label, real* value, real min, real max, const char* format)
	{
		const u64 hash = HashID(id);
		const bool bHovered = IsMouseOver(rect);
		const real hover = HoverAmount(hash, bHovered || m_ActiveID == hash);
		const real radius = UITheme::CORNER_RADIUS * m_Scale;
		FillRect(rect, glm::mix(UITheme::WIDGET, UITheme::WIDGET_HOVER, hover), radius);
		Text(label, rect.x + 20.0f * m_Scale, rect.y + rect.h * 0.5f, 18.0f * m_Scale, UITheme::TEXT_MAIN);

		char valueText[64];
		snprintf(valueText, ARRAY_LENGTH(valueText), format, *value);
		Text(valueText, rect.x + rect.w - 18.0f * m_Scale, rect.y + rect.h * 0.5f, 18.0f * m_Scale, UITheme::TEXT_DIM, TextAlign::RIGHT);

		// Track in the middle
		const real trackLeft = rect.x + rect.w * 0.42f;
		const real trackWidth = rect.w * 0.42f;
		const real trackY = rect.y + rect.h * 0.5f;
		const real t = glm::clamp((*value - min) / (max - min), 0.0f, 1.0f);
		const real trackHeight = 6.0f * m_Scale;
		FillRect(UIRect(trackLeft, trackY - trackHeight * 0.5f, trackWidth, trackHeight), glm::vec4(0.3f, 0.31f, 0.35f, 1.0f), trackHeight * 0.5f);
		FillRect(UIRect(trackLeft, trackY - trackHeight * 0.5f, trackWidth * t, trackHeight), UITheme::ACCENT, trackHeight * 0.5f);
		Circle(glm::vec2(trackLeft + trackWidth * t, trackY), (9.0f + 3.0f * hover) * m_Scale, UITheme::TEXT_MAIN);

		if (m_bMousePressed && bHovered)
		{
			m_ActiveID = hash;
		}
		if (m_ActiveID == hash && m_bMouseDown)
		{
			const real newT = glm::clamp((m_MousePos.x - trackLeft) / trackWidth, 0.0f, 1.0f);
			*value = min + (max - min) * newT;
			return true;
		}
		return false;
	}

	bool GameUI::TextField(const char* id, const UIRect& rect, std::string* text, u32 maxLength, const std::string& placeholder /* = "" */)
	{
		const u64 hash = HashID(id);
		const bool bHovered = IsMouseOver(rect);
		const bool bFocused = m_FocusedID == hash;
		const real hover = HoverAmount(hash, bHovered || bFocused);
		const real radius = UITheme::CORNER_RADIUS * m_Scale;

		if (m_bMousePressed)
		{
			if (bHovered)
			{
				m_FocusedID = hash;
			}
			else if (bFocused)
			{
				m_FocusedID = 0;
			}
		}

		FillRect(rect, glm::mix(UITheme::WIDGET, UITheme::WIDGET_HOVER, hover), radius);
		if (bFocused)
		{
			FillRect(UIRect(rect.x, rect.y + rect.h - 3.0f * m_Scale, rect.w, 3.0f * m_Scale), UITheme::ACCENT, 1.5f * m_Scale);
		}

		bool bSubmitted = false;
		if (bFocused)
		{
			for (char c : g_InputManager->GetTypedText())
			{
				if (text->size() < maxLength)
				{
					*text += c;
				}
			}
			if (g_InputManager->GetKeyPressed(KeyCode::KEY_BACKSPACE, true) && !text->empty())
			{
				text->pop_back();
			}
			if (g_InputManager->GetKeyPressed(KeyCode::KEY_ENTER))
			{
				bSubmitted = true;
				m_FocusedID = 0;
			}
		}

		const real textX = rect.x + 18.0f * m_Scale;
		const real textY = rect.y + rect.h * 0.5f;
		if (text->empty() && !bFocused)
		{
			Text(placeholder, textX, textY, 18.0f * m_Scale, UITheme::TEXT_DIM);
		}
		else
		{
			Text(*text, textX, textY, 18.0f * m_Scale, UITheme::TEXT_MAIN);
			if (bFocused && glm::fract(m_Time * 1.2f) < 0.6f)
			{
				// Caret
				const real caretX = textX + MeasureText(*text, 18.0f * m_Scale) + 2.0f * m_Scale;
				FillRect(UIRect(caretX, textY - 13.0f * m_Scale, 2.0f * m_Scale, 26.0f * m_Scale), UITheme::ACCENT);
			}
		}
		return bSubmitted;
	}

	bool GameUI::ListItem(const char* id, const UIRect& rect, const std::string& title, const std::string& subtitle, bool bSelected)
	{
		const u64 hash = HashID(id);
		const bool bHovered = IsMouseOver(rect);
		const real hover = HoverAmount(hash, bHovered);
		const real radius = UITheme::CORNER_RADIUS * m_Scale;
		glm::vec4 colour = glm::mix(UITheme::WIDGET, UITheme::WIDGET_HOVER, hover);
		FillRect(rect, colour, radius);
		if (bSelected)
		{
			FillRect(UIRect(rect.x, rect.y, 5.0f * m_Scale, rect.h), UITheme::ACCENT, 2.5f * m_Scale);
		}
		Text(title, rect.x + 22.0f * m_Scale, rect.y + rect.h * 0.36f, 20.0f * m_Scale, bSelected ? UITheme::ACCENT : UITheme::TEXT_MAIN);
		Text(subtitle, rect.x + 22.0f * m_Scale, rect.y + rect.h * 0.7f, 14.0f * m_Scale, UITheme::TEXT_DIM);

		if (WasClicked(rect))
		{
			PlayClick();
			return true;
		}
		return false;
	}
} // namespace flex
