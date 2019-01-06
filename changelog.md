
_2019_
January
- 
- Replace Assimp with tinygltf


_2018_
December
- Add carts that ride along tracks (create with C, place onto nearest track with SPACE)
- Add RenderDoc API integration to allow capturing frames with a keypress (F9)
- Add support for time dilation
- Add support for pausing and single stepping simulation
- Allow player to edit tracks
- Save track data to file
- Save bootup times to file (`saved/bootup-times.log`)
- Fix player direction handling at junctions

November
- Allow screenshots to be taken in-game (using numpad 9 key)
- Add basic controls to allow player to place tracks (Y to enter placement mode, RT to place, LT to finalize, X to lock in/out, LT/RT to move along track)
- Add basic keyboard movement inputs
- Bring up rename UI on F2
- Heavily optimize debug rendering
- Add basic sound synthesis (play notes using 4-9 keys)
- Add ability to change overhead camera zoom level (L & R joystick press)
- Improve "overhead" camera to follow player position & rotation
- Allow player to switch direction on a track by holding right joystick in a direction

October
- Add tracks (Bezier Curves which the player can move along for speedy travel) (X button to connect/disconnect)
- Separate depth-aware editor objects from depth-unaware
- Triplanar mapping shader
- Prevent input handling when window doesn't have focus
- Add first person controller (with separate pitch for look dir & capsule)
- Add track junctions to handle switching tracks
- Improve transform gizmo implementation (namely rotation & scale)
	- Fade out axes when facing head-on
