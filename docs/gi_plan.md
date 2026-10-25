# Basic GI plan (no hardware ray tracing)

Status: plan only, nothing implemented yet.

## Where lighting stands today

- **Sun/moon**: one directional light with cascaded shadows and cloud shadows.
- **Terrain** (forward, `vk_terrain.frag`) gets its sky light from the sky gradient colours, scaled by a per-vertex
  *sky visibility* (vertex weight `w`). `TerrainGeneration::ComputeSkyVisibility` computes it on the CPU for LOD 0/1
  chunks by casting 5 x 3 density samples upward, so it is effectively a coarse AO/cave term.
- **Deferred objects** (buildings, props, vehicles, tools) take ambient from one irradiance cubemap captured by the
  reflection probe. It is captured once and scaled by the current sky brightness, then multiplied by SSAO and a
  partial sun shadow term (`ambientShadowAmount`).
- **Missing**:
  - Bounce light, so sunlit ground doesn't brighten nearby surfaces.
  - Any occlusion of the sky for objects (they are lit as if outdoors even inside a cave or a closed building).
  - Contact occlusion between objects and terrain. SSAO only sees the G-buffer, and terrain is drawn in the forward
    pass.

## Options

### A. Sky visibility volume plus bent-normal ambient (recommended first step)
Store sky visibility in a coarse 3D texture around the camera, e.g. 64x32x64 cells of 2 m (128 m x 64 m x 128 m),
R8 or RG8 (visibility plus a dominant open direction). Fill it from the data terrain generation already has:
evaluate the same upward density rays as `ComputeSkyVisibility` per cell on the background terrain pool, and
re-fill only slices that scroll into view or chunks that get edited. Building pieces mark their cells as partly
occluded when they are placed.
- **Objects**: `ambient *= visibility(posWS)`, and sample the irradiance cubemap along the bent normal instead of
  N. This fixes the most visible problem: objects in caves and buildings glowing with sky light.
- **Terrain**: keep its per-vertex value, but take the minimum with the volume so buildings shade the ground under
  them (this grounds them much better than SSAO can).
- **Cost**: about 0.5 MB of GPU memory and one trilinear fetch per pixel. CPU fill is about 130k cells x 15 density
  samples, spread over frames on the background pool (same as chunk generation).
- **Gives no bounce colour.** It is purely an occlusion term, but the cheapest big win.

### B. Low-resolution irradiance probe grid (DDGI-lite, rasterized)
A camera-centred grid (e.g. 16x8x16 probes, 8 m spacing) of L1 spherical-harmonic irradiance (12 floats per probe).
Each frame, update about 64 probes by rendering a tiny 6 x 16² cubemap per probe with a cheap forward shader
(terrain plus the large objects, sun with its shadow map, sky), then project the result to SH in a compute pass.
Shading interpolates 8 probes. A visibility weight from the volume in option A, or a per-probe depth moment like
DDGI, reduces light leaking through walls.
- Gives real one-bounce colour (green from grass, warm light inside buildings from torches and campfires) and
  occlusion, and adds multiple bounces over time because probes see each other's lighting.
- **Cost**: 64 probes x 6 faces x 16² = about 100k pixels of extra raster per frame (around 0.3-0.6 ms), plus the
  draw call overhead of 384 small views. That overhead is the main risk with this engine's per-draw descriptor
  setup, so batch the views as a cubemap array with multiview, or render only terrain into the probes.
- Most code: probe scheduling, SH projection, leak handling, and integration in both the terrain and deferred
  shaders.

### C. Screen-space GI lite (SSGI on the SSAO pass)
Extend the SSAO kernel. For samples that hit, also fetch the lit colour of the previous frame (reprojected) at the
hit point and accumulate it, cosine weighted, as one bounce. Denoise with the existing SSAO blur and TAA history.
- Cheap to add (it reuses the SSAO samples, so +1 colour fetch per sample, about 0.3 ms at half resolution) and
  gives nice local colour bleeding at contact points.
- Screen-space only: light disappears when its source leaves the screen, and it does nothing for terrain until
  terrain depth is in the G-buffer (see below). It doesn't help with caves or interiors, which are the biggest
  current artifact.

## Recommendation
Do **A** first, then **B** if bounce colour is wanted. Skip C unless the probes turn out too coarse up close.

Steps for A:
1. **Terrain depth in the G-buffer.** Add a depth-only terrain draw in the deferred pass (same vertex shader, no
   fragment output), or move terrain to the deferred path. This lets SSAO see terrain and object contacts and
   enables 3. It is also needed for good SSAO grounding of buildings on terrain, which today's SSAO can't do.
2. **Sky visibility volume.**
   - Add an `R8` 3D texture plus a scrolling origin to the global constant UBO.
   - Add a CPU job that fills slices using the `TerrainGeneration` density and edits.
   - Upload changed slices with a staging buffer.
   - Add a debug overlay mode that shows it (like `FLEX_DEBUG_OVERLAY=4` does for terrain).
3. **Deferred combine**: `ambient *= SampleSkyVisibility(posWS + N * 0.5)`. Use the bent normal (the RG channels)
   for the irradiance lookup.
4. **Terrain**: `skyVisibility = min(vertexSkyVisibility, volume)`, so built roofs and walls darken the ground and
   the interiors under them.
5. **Building placement**: rasterize each placed piece's bounds into the volume, about 0.4 occlusion per filled cell.
6. **Validate** with the Macbeth scene (`scene_macbeth.json`): ambient-only patches under a roof should drop by the
   expected fraction. Also compare cave screenshots before and after.

Rough sizes: steps 1-4 are about 2 days. B adds about 4-5 days on top, reusing A's volume for leak prevention.
