# Crafting

How the player turns what they find into new items. Recipes are data (`FlexEngine/resources/recipes.txt`), the code
is in `Crafting.{hpp,cpp}` (recipes, the crafting panel, station screens & placement) and
`Scene/Objects/Stations.{hpp,cpp}` (chests & furnaces).

## What we want from it

- Exploration is the source of progress: finding a new material (an ore in a cave, berries in a new biome, quartz in
  a treasure chest) should immediately open up something new to make.
- Readable without a wiki. The player shouldn't have to know or guess shapes or secret combinations.
- Cheap to extend: adding an item & what it's made from is a text edit, no code.
- Fits the game's UI: one immediate mode toolkit (`GameUI`), mouse driven, no drag & drop needed.

## Approaches considered

| Game | How it works | Good | Bad for us |
|---|---|---|---|
| Minecraft | 3x3 grid, items placed in a shape | Discovery by experiment, iconic | Shapes are arbitrary & need a wiki (or the later recipe book). Needs a drag & drop grid UI, and every recipe needs a shape designed. Shapes add nothing to a game about exploring terrain. |
| Zelda: BotW / TotK | No crafting: gear is found; cooking throws up to 5 items in a pot & combines their effects; Fuse attaches things | Rewards experiment, very little UI | Needs an effect system (hearts, buffs) we don't have; doesn't help build bases or tools. A good later model for *cooking* only. |
| Valheim | Recipe list; a recipe appears once you've picked up one of its ingredients; stations (workbench, forge) gate tiers & must be nearby | Exploration directly unlocks recipes; no guessing; stations are places in your base | Lists can get long (solved with categories/filters). |
| Subnautica | Fabricator with a categorized recipe tree; blueprints found by scanning fragments | Very clear; finding blueprints drives exploration | Scanning needs its own system. |
| Terraria | Every recipe you can make with what's in your inventory, near the right station | Zero friction | Hides what you *could* make, so it's less of a goal to work towards. |
| Factorio / Satisfactory | Recipe list + machines processing over time | Clear timing, automation | Automation is beyond scope (for now). |

## Choice: a recipe list unlocked by discovery, with stations (Valheim style)

1. **Recipe list, not a grid.** Open the crafting panel ([I]) anywhere. Each row shows the output, its ingredients
   (have/need, coloured) & a Craft button. No shapes to memorize; the UI is a list (what `GameUI` is good at).
2. **Discovery unlocks recipes.** A recipe is listed once the player has held any of its ingredients (or its
   output). Picking up something new announces the recipes it unlocks ("New recipe: Furnace"), so the reward for
   exploring a cave or a biome is concrete. Discovered items are saved with the world (in the player's inventory).
3. **Stations gate the processing.** Hand recipes are instant. Furnace recipes (smelting ore, cooking food) happen in
   a placed furnace over time while it burns fuel, so a base & fuel matter. Recipes name their station, so more
   stations (workbench, loom, cooking pot) are a line in `recipes.txt` & a small type.
4. **Placeable outputs are built, not carried.** Crafting a chest or furnace enters a placement mode with a
   hologram (like campfires), and the ingredients are only taken when it's placed.

BotW-style cooking (combining any ingredients for effects) can be layered on later as a furnace/cooking pot mode once
food has effects; the recipe list stays the backbone.

## recipes.txt

```
hand: chest = wood x10
furnace: iron ingot = mineral deposit iron @ 12
fuel: charcoal = 40
```

- `<station>: <output> [xN] = <input> [xN], <input> [xN] [@ <seconds>]`, station `hand` or `furnace`.
- Names are prefab names (`resources/prefabs/*.json`); shown names come from `Item_*` in `loc/en.txt`
  (via `Loc::ItemName`). Recipes naming an unknown item are skipped with a warning (so others' items can be listed
  before they exist).
- Furnace recipes take one input item at a time & `@` seconds each (default 10).
- `fuel: <item> = <seconds>`: what furnaces burn.

## Stations

- **Chest**: 24 slots, saved with the world. [E] opens it: click an item on either side to move the stack across;
  Store all / Take all. An empty chest can be dismantled (refunds its recipe).
- **Furnace**: input, fuel & output slots. Burns one fuel item at a time while there's something to smell & room for
  the result; glows while burning. State (slots, burn & progress) is saved with the world.

## Getting raw materials

- Wood (trees), charcoal (burnt trees), cactus: chopping.
- Stone: digging rock with the pickaxe gives stone, and sometimes iron or tin ore (deterministic from the spot).
- Quartz, aluminum, tin, olivine: treasure chests.

## Testing

`FLEX_TEST_CRAFTING=1` (in a world): crafts & places a chest & furnace in front of the player, stores & retrieves
items, smelts iron with charcoal, crafts a tool, screenshots the panels (`saved/screenshots/crafting_*.png`) & prints
PASS/FAIL. `FLEX_TEST_CRAFTING=verify` on a later run (same world) checks the chest & furnace were saved & loaded.
