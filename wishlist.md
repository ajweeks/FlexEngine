# Wish list

Feature ideas, mostly for exploring the streamed terrain. They're not commitments, just a list to pick from.

## Traversal (the huge mountains need these)
Implemented (see `Traversal.hpp`): jumping, a **paraglider** with thermals & ridge lift, a **wingsuit**, a **grappling hook**, stamina based **climbing** and **skiing/sliding** (fastest on snow & sand).

Ideas building on those:
- **Ziplines**: player-built cables between two anchor points, which would reuse track/wire placement. A grappling hook rope could be fixed in place to make one.
- **Visible thermals**: dust/leaves or circling birds in rising air (lift is computed in `Traversal::ComputeLift`), so they can be found without the variometer.
- **Ski tracks & snow spray** particles.
- **Boats** for lakes and oceans (water surfaces are flat at a known level, so buoyancy is simple).
- **Vehicle upgrades** for off-road driving: suspension travel, tire grip per material (sand, snow, rock).

## World
- **Rivers**: see the design notes below.
- **Vegetation**: trees and grass scattered per biome (instanced, with density from biome and slope). It would also make boreal and wetland biomes distinct.
- **Weather and seasons**: snow line and lake ice varying with time, rain, and fog banks in valleys.
- **Points of interest**: ruins, caves marked on the map, summit markers, and discoverable map regions (fog of war on the minimap).
- **Resources in caves**: mineral deposits placed by depth and biome (the mining system exists already).
- **Waterfalls** where rivers or lakes meet cliffs.

## Player tools
- **Terrain tools**: flatten and smooth brushes, a bigger digging radius with upgrades, and a fill brush that follows the surface.
- **Compass, waypoints, and a full-screen map** built from the terrain map window.

## Rivers: design notes
Rivers are harder than lakes because the water surface must slope downhill continuously and stay consistent across chunks at every LOD. Each chunk must also be able to generate them independently, without global simulation. Proposed approach:

1. **Coarse river network per region.** Divide the world into large cells (e.g. 16 km). For each cell, deterministically from the world seed:
   - pick a few sources at high ground (springs below massifs or mountains);
   - trace each source downhill over a coarse (~100 m) heightfield grid using steepest descent, with lake and ocean cells as sinks;
   - smooth each traced path into a spline.

   A cell also needs neighbor cells' rivers that flow into it, so trace with a margin and cache results per cell, the same way lakes cache their level.
2. **Monotonic water level.** Along each spline, store the water level as a running minimum of terrain height minus a few meters, so it never rises downstream. Make pools and steps where the terrain drops sharply (waterfalls).
3. **Carving.** In `SampleColumn`, find the distance to the nearest river spline segment (using a spatial hash of segments per cell). Blend the height down to `waterLevel - depth(width)` inside the channel, with banks, the same way lakes carve their basins. Width grows with downstream distance or catchment.
4. **Water mesh.** Rivers can't use the flat marching-squares water surface. Emit a ribbon along the spline, clipped per chunk by containment of the segment's midpoint. Give it a UV flow direction for animated normals.
5. **Gameplay.** Rivers give natural paths through mountains (valleys), places for boats, and obstacles for vehicles.

Rivers can be valid only where the coarse trace reaches a lake or ocean, so dead-end rivers never appear. Cost is mostly per-cell tracing, which is cached and cheap per column after that (a nearest-segment query).
