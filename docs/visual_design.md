# Flex visual design guide

Flex's menus and HUD should feel modern and clean, like a well-made tool, and also warm, like a camp at dusk. They
should be quick to read without looking clinical. The world is the main subject. UI sits on top of it as calm, dark
glass with one warm accent.

All values below are defined in code in `FlexEngine/include/GameUI.hpp` (`namespace UITheme`). Use the named constants
there and never write new literal colours or sizes in screen code. If a new role is needed, add it to `UITheme` and to
this document.

## Principles

1. **The world first.** Menus leave the world visible: scrims fade it out rather than block it, and the HUD keeps to
   the corners and edges.
2. **One accent.** Ember means "act here" or "you are here". If everything is orange, nothing is.
3. **Warm neutrals, never pure black or white.** Surfaces are dark, slightly blue-green "night" greys, and text is a
   warm off-white. Together they keep the UI from looking cold.
4. **Soft, consistent geometry.** Rounded corners from a small set of radii, soft shadows and no hard bevels.
5. **Quiet motion.** Hover states ease in (about 70 ms), and screens slide in over about 300 ms. Nothing bounces or
   loops for attention.

## Palette

Colours are sRGB hex values, as displayed. `UITheme::Hex(0xRRGGBB, alpha)` converts them.

### Surfaces

| Token          | Hex       | Alpha | Role |
|----------------|-----------|-------|------|
| `INK`          | `#111318` | 1.0   | Scrims, backdrops and outlines around markers. Used translucent over the world (0.15–0.72) |
| `SURFACE`      | `#1A1D23` | 0.90  | Panels (bottom of the panel gradient) |
| `SURFACE_TOP`  | `#23272E` | 0.92  | Top of the panel gradient, HUD plates, map frame |
| `RAISED`       | `#2A2F37` | 0.94  | Widgets on a panel: buttons, rows, fields |
| `RAISED_HOVER` | `#363C46` | 0.96  | Hovered or selected widgets |
| `OUTLINE`      | `#48505C` | 1.0   | Slider and toggle tracks, dividers, hairlines |

### Text

| Token            | Hex       | Role |
|------------------|-----------|------|
| `TEXT_PRIMARY`   | `#F3EFE7` | Titles, labels and values the player must read |
| `TEXT_SECONDARY` | `#ADA9A0` | Subtitles, hints, key legends and resting values |
| `TEXT_DISABLED`  | `#6E6B66` | Placeholders, disabled labels |
| `TEXT_ON_ACCENT` | `#1C140B` | Text on ember fills (primary buttons) |

### Accent: ember

| Token        | Hex       | Role |
|--------------|-----------|------|
| `EMBER`      | `#F4A23C` | Primary buttons, selection bars, slider fills, toggles, focus, heading marker |
| `EMBER_DEEP` | `#D9822B` | Bottom of a primary button's gradient, pressed state |
| `EMBER_GLOW` | `#FFC56E` | Hovered primary button, values being changed, notifications |

### Semantic

| Token  | Hex       | Role |
|--------|-----------|------|
| `SKY`  | `#6CB6D9` | Information, water and cool status (e.g. oxygen, cold) |
| `MOSS` | `#86C07A` | Success and confirmations (saved, crafted) |
| `CLAY` | `#E0614F` | Danger, destructive actions, damage, low health |
| `GOLD` | `#F5CE5E` | Treasure and rewards (unopened chests on the map) |

Opened or "spent" items use a desaturated tan (`#8C8273`) rather than a new colour.

### Contrast

`TEXT_PRIMARY` on `SURFACE` is about 14.7:1. `TEXT_SECONDARY` on `SURFACE` is about 7:1, so it is fine for small text.
`TEXT_ON_ACCENT` on `EMBER` is about 8.7:1. Don't put `TEXT_SECONDARY` on `RAISED_HOVER` at caption size, and never put
text straight onto the world without a scrim or plate behind it.

## Typography

Two faces: the heading font (`ui-heading`) for titles and the body font (`ui`) for everything else. Sizes are cap
heights in pixels at 1080p and are multiplied by `GameUI::Scale()`.

| Token             | Size | Font    | Use |
|-------------------|------|---------|-----|
| `TEXT_DISPLAY`    | 80   | heading | Screen titles ("FLEX", "Settings") |
| `TEXT_H1`         | 44   | heading | Dialog titles ("Paused"), loading screen |
| `TEXT_H2`         | 32   | heading | Section titles ("Map") |
| `TEXT_BODY_LARGE` | 20   | body    | Buttons, list item titles, screen subtitles |
| `TEXT_BODY`       | 18   | body    | Widget labels and values, text fields |
| `TEXT_SMALL`      | 16   | body    | Hints, field captions, hover info |
| `TEXT_CAPTION`    | 14   | body    | Metadata, key legends, scale labels |

- Use sentence case ("Save & quit to menu"). Only the game's name is all caps.
- Write key legends as `[Key] Action`, separated by three spaces. Separate metadata with `   |   `.
- Don't use more than three sizes on one screen.

## Spacing and layout

A 4 px grid: `SPACE_XS` 4, `SPACE_S` 8, `SPACE_M` 12, `SPACE_L` 16, `SPACE_XL` 24, `SPACE_XXL` 32.

- Menu content is left aligned at `SCREEN_MARGIN` (120). Titles sit at y 220, with a 64×4 ember underline below.
- Panels have 24 px padding. Rows are `ROW_HEIGHT` (62) tall and buttons are `BUTTON_HEIGHT` (56) tall, with
  `SPACE_S`–`SPACE_M` gaps.
- Text inside widgets starts `SPACE_XL` from the widget's edge.
- HUD elements stay `HUD_MARGIN` (32) from the screen's edges.

## Shape

| Token      | Radius | Use |
|------------|--------|-----|
| `RADIUS_S` | 4      | Bars, markers, frames |
| `RADIUS_M` | 10     | Widgets (buttons, rows, fields, hotbar slots) |
| `RADIUS_L` | 16     | Panels and HUD plates |
| pill       | h / 2  | Toggle tracks, slider tracks, progress bars |

Panels get a soft drop shadow (24 px blur, 40% opacity) and a faint 1 px highlight along their top edge. Widgets get a
lighter shadow (8 px blur) only when they can be clicked.

## States

| State    | Treatment |
|----------|-----------|
| Rest     | `RAISED` fill, `TEXT_PRIMARY` label |
| Hover    | Eases to `RAISED_HOVER`, grows 3 px, and a 4 px ember bar slides in on the left (secondary buttons) |
| Primary  | `EMBER` → `EMBER_DEEP` gradient, `TEXT_ON_ACCENT` label; hover brightens to `EMBER_GLOW` |
| Active   | A slider's value text turns `EMBER_GLOW` while it's dragged; a confirm tone plays on release where the change is audible (volume) |
| Focus    | A 3 px ember underline on text fields, with a blinking ember caret |
| Selected | `RAISED_HOVER` fill, a 4 px ember bar on the left and an ember title |
| Disabled | `RAISED` at 50% alpha, `TEXT_DISABLED` label, no hover |
| Danger   | Confirm destructive actions in place: the button's label changes to "Really delete?" and a second click confirms |

Every click plays the UI click sound. Don't stack sounds on hover.

## HUD and iconography

- **Placement:** status at the top right (minimap), inventory and hotbar at the bottom center, transient notifications
  at the top center, interaction prompts just below the crosshair. Never cover the screen's center.
- **Plates:** HUD elements sit on `SURFACE` plates with `RADIUS_L` corners, never bare on the world. For example, the
  minimap is a 280 px map on a plate with 6 px padding.
- **Markers:** simple geometric shapes (dots and squares) with an `INK` outline so they read on any terrain.
  - Player: a `TEXT_PRIMARY` dot. Heading: a small `EMBER` dot ahead of it.
  - Treasure: a `GOLD` square, or tan once opened.
  - Off-view player on the map: a smaller dot pinned to the map's edge.
- **Icons:** flat, single-weight silhouettes in `TEXT_PRIMARY`, with no outlines, gradients or perspective. Show item
  counts in the bottom right of the slot in `TEXT_PRIMARY` at caption size.
- **Maps:** north is up. Show the scale in words ("3.2 km across"). Pan with the middle mouse button, re-center with
  C, zoom with the scroll wheel.
- **Engine overlays:** debug text, the "FLEX ENGINE" watermark and gizmos are hidden in game mode.

## Do and don't

**Do**

- Use `UITheme` tokens for every colour and size.
- Fade the world with `INK` gradients, not flat black boxes or hard-edged rectangles.
- Keep ember for the single most important action on a screen, plus selection and focus.
- Group related settings into one panel and leave space between groups.
- Make hit areas whole rows (a toggle's row toggles it).

**Don't**

- Use pure black (`#000`) or pure white (`#FFF`) fills.
- Use more than one primary (ember) button per panel.
- Add borders around widgets. Separate them with fill and spacing instead.
- Put text straight onto the world without a scrim or plate.
- Animate for attention (pulsing, bouncing or looping).
- Introduce a new colour for a one-off. Map it to a semantic token instead.
