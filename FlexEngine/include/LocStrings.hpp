#pragma once

// Every piece of text shown to the player, as X(ID, "English text"). English is the source language: other languages'
// tables (see Localization.cpp) translate these IDs, & any they leave out fall back to the English text here.
//
// To add a string: add a line here (keep related strings together, IDs are Area_Name), then use LOC(Area_Name) for a
// const char*, or LOCF(Area_Name, args...) when the text has printf-style arguments (%s takes a const char*).
// Strings are looked up by index, so LOC() is free to use every frame.
#define FLEX_LOC_STRINGS(X) \
	/* Main menu */ \
	X(MainMenu_Tagline, "An endless world to explore, climb & build in") \
	X(MainMenu_Continue, "Continue: %s") \
	X(MainMenu_NewWorld, "New world") \
	X(MainMenu_Worlds, "Worlds") \
	X(MainMenu_Settings, "Settings") \
	X(MainMenu_Editor, "Engine test scenes") \
	X(MainMenu_Quit, "Quit") \
	X(MainMenu_Version, "%s v%s") \
	/* Worlds */ \
	X(Worlds_Title, "Worlds") \
	X(Worlds_Subtitle, "Every world is endless, & made from its seed") \
	X(Worlds_ListSubtitle, "Seed %d   |   Played %s   |   Last played %s") \
	X(Worlds_Play, "Play") \
	X(Worlds_New, "New world") \
	X(Worlds_Delete, "Delete") \
	X(Worlds_ConfirmDelete, "Really delete?") \
	X(Worlds_DefaultName, "World %d") \
	X(Worlds_FirstWorldName, "My world") \
	/* World stats (beside the world list) */ \
	X(Stats_Created, "Created") \
	X(Stats_LastPlayed, "Last played") \
	X(Stats_TimeInWorld, "Time in world") \
	X(Stats_AreaExplored, "Area explored") \
	X(Stats_DistanceTravelled, "Distance travelled") \
	X(Stats_RailLaid, "Rail laid") \
	X(Stats_TorchesPlaced, "Torches placed") \
	X(Stats_PiecesBuilt, "Pieces built") \
	X(Stats_ChestsOpened, "Chests opened") \
	X(Stats_Seed, "Seed") \
	X(Stats_Unknown, "Unknown") \
	X(Units_Km, "%.2f km") \
	X(Units_SquareKm, "%.2f sq km") \
	X(Units_Minutes, "%d min") \
	X(Units_HoursMinutes, "%d h %d min") \
	/* New world */ \
	X(NewWorld_Title, "New world") \
	X(NewWorld_Subtitle, "Name it & pick a seed (or keep the random one)") \
	X(NewWorld_Name, "World name") \
	X(NewWorld_Seed, "Seed") \
	X(NewWorld_SeedPlaceholder, "Random") \
	X(NewWorld_Random, "Random") \
	X(NewWorld_Create, "Create & play") \
	/* Settings */ \
	X(Settings_Title, "Settings") \
	X(Settings_Subtitle, "Saved automatically") \
	X(Settings_Fov, "Field of view") \
	X(Settings_FovValue, "%.0f deg") \
	X(Settings_MouseSensitivity, "Mouse sensitivity") \
	X(Settings_InvertMouse, "Invert mouse") \
	X(Settings_Volume, "Volume") \
	X(Settings_TerrainDetail, "Terrain detail: %s") \
	X(Settings_QualityLow, "Low") \
	X(Settings_QualityMedium, "Medium") \
	X(Settings_QualityHigh, "High") \
	X(Settings_QualityUltra, "Ultra") \
	X(Settings_ShowMinimap, "Show minimap") \
	X(Settings_Language, "Language: %s") \
	/* Common */ \
	X(Common_Back, "Back") \
	/* Loading */ \
	X(Loading_Terrain, "Generating terrain...") \
	/* Pause menu */ \
	X(Pause_Title, "Paused") \
	X(Pause_Played, "%s   |   %s played") \
	X(Pause_Resume, "Resume") \
	X(Pause_Map, "Map & teleport") \
	X(Pause_Settings, "Settings") \
	X(Pause_QuitToMenu, "Save & quit to menu") \
	X(Pause_QuitGame, "Save & quit game") \
	/* Map */ \
	X(Map_Title, "Map") \
	X(Map_Controls, "[Scroll] Zoom   [Middle drag] Pan   [C] Re-center   [Click] Teleport   [Esc / M] Close") \
	X(Map_Scale, "%.1f km across") \
	X(Map_Mapping, "Mapping the area...") \
	X(Map_Hover, "%s   |   %.0fm high   |   %.1fkm away   |   Click to teleport") \
	X(Map_Teleported, "Teleported %.1fkm") \
	/* Interaction prompts */ \
	X(Prompt_OpenChest, "[E] Open chest") \
	X(Prompt_Drive, "[E] Drive") \
	X(Prompt_Row, "[E] Row") \
	X(Prompt_UseTerminal, "[E] Use terminal") \
	/* Notifications */ \
	X(Notify_Treasure, "Treasure! Found %s") \
	X(Notify_TreasureEmpty, "Treasure! Found nothing (it was empty)") \
	X(Notify_ItemCount, "%dx %s") \
	X(Notify_CampfireReady, "You have enough wood for a campfire: press [H] to build one") \
	X(Notify_CampfireNeedsWood, "A campfire needs %d wood (you have %d). Chop down trees to get some.") \
	X(Notify_CampfireBuilt, "Campfire built. Hold [E] next to it to light it.") \
	X(Notify_CampfireLit, "The campfire is lit. It'll burn for %d minutes.") \
	/* Woodcraft */ \
	X(Woodcraft_BuildCampfire, "[LMB] Build campfire (%d wood)   [RMB]/[H] Cancel") \
	X(Woodcraft_FindGround, "Find flat, dry ground for the campfire   [RMB]/[H] Cancel") \
	X(Woodcraft_ChopTree, "[LMB] Chop tree") \
	X(Woodcraft_ChopCactus, "[LMB] Chop cactus") \
	X(Woodcraft_QuickerWithAxe, " (quicker with an axe)") \
	X(Woodcraft_LightCampfire, "[Hold E] Light campfire") \
	X(Woodcraft_CampfireBurning, "Campfire burning (%d:%02d left)") \
	/* Digging & placing terrain (pickaxe & shovel) */ \
	X(TerrainTool_DigLoose, "[Hold LMB] Dig (quick in soil, sand & gravel)") \
	X(TerrainTool_DigRock, "[Hold LMB] Dig (quick in rock)") \
	X(TerrainTool_Place, "[Hold RMB] Place %s") \
	X(TerrainTool_ChangeMaterial, "[R] / [Shift+R] Change material") \
	/* Build mode */ \
	X(Build_Title, "BUILDING: %s") \
	X(Build_TitleMaterial, "BUILDING: %s (%s)") \
	X(Build_Controls, "[LMB] Place   [RMB] Remove   [Scroll/1-7] Piece   [R] Rotate   [N] Material   [B] Exit") \
	X(Build_Floor, "Floor") \
	X(Build_Wall, "Wall") \
	X(Build_Roof, "Roof") \
	X(Build_Stairs, "Stairs") \
	X(Build_Torch, "Torch") \
	X(Build_Ladder, "Ladder") \
	X(Build_Rope, "Rope") \
	X(Build_Wood, "Wood") \
	X(Build_Stone, "Stone") \
	/* Track building */ \
	X(Track_BuildingTitle, "TRACK BUILDING - %d nodes placed") \
	X(Track_PlaceNode, "[%s] Place node") \
	X(Track_UndoNode, "[%s] Undo last node") \
	X(Track_Finish, "[%s] Finish track") \
	X(Track_FinishExit, "[%s] Finish & exit") \
	X(Track_AimAtGround, "Aim at the ground to place nodes") \
	X(Track_SnappedLoop, "Snapped - placing here closes the loop & finishes the track") \
	X(Track_SnappedBranch, "Snapped - new track will branch from this node") \
	X(Track_SnappedJoin, "Snapped - placing here joins & finishes the track") \
	X(Track_EditingTitle, "TRACK EDITING") \
	X(Track_GrabPoint, "[%s] Grab point") \
	X(Track_ReleasePoint, "[%s] Release point") \
	X(Track_DeleteNode, "[%s] Delete hovered node") \
	X(Track_DeleteTrack, "[%s] Delete highlighted track") \
	X(Track_Exit, "[%s] Exit") \
	X(Track_ForkLeft, "FORK AHEAD - going left") \
	X(Track_ForkRight, "FORK AHEAD - going right") \
	X(Track_ForkStraight, "FORK AHEAD - going straight") \
	X(Track_ChooseFork, "[%s] / [%s] Choose left / right") \
	X(Track_PrimaryBinding, "LMB / %s") \
	X(Input_Unbound, "Unbound") \
	/* Item names (see Loc::ItemName) */ \
	X(Item_Pickaxe, "Pickaxe") \
	X(Item_Axe, "Axe") \
	X(Item_Shovel, "Shovel") \
	X(Item_Wood, "Wood") \
	X(Item_DuneBuggy, "Dune buggy") \
	X(Item_RowBoat, "Row boat") \
	X(Item_Headlamp, "Headlamp") \
	X(Item_SolarPanel, "Solar panel") \
	X(Item_Battery, "Battery") \
	X(Item_Lamp, "Lamp") \
	X(Item_Quartz, "Quartz") \
	X(Item_Aluminum, "Aluminum") \
	X(Item_Tin, "Tin") \
	X(Item_Olivine, "Olivine") \
	X(Item_Iron, "Iron") \
	X(Item_Silicon, "Silicon") \
	X(Item_Stone, "Stone") \
	/* Traversal HUD (paraglider, wingsuit, climbing, skiing, grappling hook) */ \
	X(Traversal_GliderTitle, "PARAGLIDER") \
	X(Traversal_GliderStats, "Height %.0fm   Speed %.0fkm/h   Vario %+.1fm/s") \
	X(Traversal_RisingAir, "Rising air! Circle to stay in it") \
	X(Traversal_FindRisingAir, "Find rising air: sunny slopes, ridges facing the wind") \
	X(Traversal_GliderControls, "[W/S] Speed   [Ctrl] Wingsuit   [Space] Stow") \
	X(Traversal_WingsuitTitle, "WINGSUIT") \
	X(Traversal_FlightStats, "Height %.0fm   Speed %.0fkm/h") \
	X(Traversal_WingsuitHint, "Look down to dive, pull up to trade speed for height") \
	X(Traversal_WingsuitControls, "[Release Ctrl] Open glider") \
	X(Traversal_ClimbingTitle, "CLIMBING") \
	X(Traversal_ClimbingControls, "[WASD] Climb   [Space] Leap   [Release Shift] Let go") \
	X(Traversal_SkiingTitle, "SKIING") \
	X(Traversal_SlidingTitle, "SLIDING") \
	X(Traversal_SpeedStats, "Speed %.0fkm/h") \
	X(Traversal_SkiingControls, "Look to carve   [S] Brake   [Space] Jump   [Release Ctrl] Stand") \
	X(Traversal_HookTitle, "GRAPPLING HOOK") \
	X(Traversal_HookControls, "[Space] Reel in   [Ctrl] Let out   [WASD] Swing   [F] Release")
