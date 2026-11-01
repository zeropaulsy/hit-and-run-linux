# Vulkan renderer (`libs/pure3d/pddi/vk/`, branch `vulkan`)

Third pddi beside GL (GL stays the default and the fallback). ini `renderer=vulkan` (Options > Graphics, next
start); `SHAR_RENDERER=vulkan|opengl` overrides.

## Goals
- Same picture as GL: GL is the reference (and dx8 in the dump behind it). Any difference is a Vulkan bug unless
  GL is wrong too.
- What Vulkan is for: Android, macOS (MoltenVK), low-power devices, loader threads without GL's shared context
  pool, later ray-traced shadows and DLSS/FSR. Not desktop speed: GL is fast enough there.
- Merge into `main` once long play-tests pass and it runs on Mesa as well as NVIDIA; GL stays default until then.

## Where it stands
- Works (NVIDIA RTX 5060, Wayland, 64-bit; also builds 32-bit): legal screen, boot movies, front end, L1 in game,
  MSAA 2/4/8x with alpha-to-coverage, mipmaps/anisotropy, user post and material shaders (SSAO, tonemap, softlit),
  gamma, window resize/minimise, mod shader hacks wired (RefractionShaderSupport, HoverCarRefraction,
  VideoTextureSupport). Validation and sync validation clean on boot, movies, front end, L1 (8x MSAA, post passes).
- Movies match GL (MSAA target kept across the resolve, see Design; edge strip: `port/notes/platform.md`).
- Not yet tested: long sessions, other levels/interiors/cutscenes/bonus game, mods in play (HoverCarRefraction,
  video textures), Mesa (AMD/Intel) drivers, X11, fullscreen switches, memcheck/ASan runs on the Vulkan path.
- Not done: pipeline cache on disk (first-use pipeline stalls), `Screenshot()`, GPU times in `SHAR_GL_STATS`
  (draws/pipelines only).

## Next
1. User play-tests on Vulkan (the same coverage GL had); compare with GL by screenshot when something looks off.
2. Mesa: an AMD/Intel GPU, or lavapipe (CPU) as a first check; `SHAR_VK_DEVICE=n` picks the device.
3. Pipeline cache on disk; memcheck/ASan on the Vulkan path.
4. Merge into `main`, then the platform goals above.

## Testing
- Shots: `SHAR_SHOT_EVERY=n` reads back the scene image before the present pass. What reaches the screen:
  `VK_INSTANCE_LAYERS=VK_LAYER_MESA_screenshot VK_LAYER_MESA_SCREENSHOT_CONFIG="frames=600/900,output_dir=DIR"`.
- `SHAR_VK_DEBUG=1` validation; add `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true` for sync hazards.
- Same frame in both: run once per `SHAR_RENDERER`, copy the user's simpsons.ini into the scratch run dir (window
  size, MSAA, filtering change the result).

## Design
- Choice made before the window exists (`WantVulkan` pre-reads simpsons.ini, linuxplatform_sdl.cpp): window gets
  `SDL_WINDOW_VULKAN`, `pvkStart` inits; failure -> window remade for GL. p3d platform.cpp picks `pddiCreateVulkan` by
  the window flag. libvulkan via `SDL_Vulkan_LoadLibrary` (no link dep); entry points in namespace `pvkfn`.
- Needs 1.3 (dynamic rendering, sync2, extended dynamic state) + `VK_KHR_push_descriptor`. `SHAR_VK_DEBUG=1`
  validation (vulkan-validationlayers), `SHAR_VK_DEVICE=n`.
- Mirrors GL: vkcon/vkmat = glcon/glmat logic (vkmat generated from glmat), keep in step. Game's `pgl*` calls
  dispatch on `pvkActive()` (gldisplay.cpp, glmat.cpp, glpipeline.cpp). glcustom shared via `pglCustomTarget`.
- GLSL = glshaders.cpp with `#define PVK 1` (z remap, gl_PointSize, attrib locations, refraction y), compiled by
  `libshar_glslang.so` (port/third_party/glslang; relaxed rules: loose uniforms -> block at binding 0, std140
  reflection; float arrays stride 16). Samplers from binding 1: both sampler and texture shifts; a shader without
  loose uniforms must give explicit bindings.
- Offscreen targets keep contents (as GL's window), the MSAA one too across its resolve (storeOp STORE: movies
  draw without clearing; DONT_CARE gave garbage depth = dithered, ghosted movie on NVIDIA). GL y-up via negative
  viewport height (images top row first, same texels as GL); Present = gamma pass into swapchain. MSAA: resolve
  via empty rendering instance with resolve attachments (depth/stencil SAMPLE_ZERO) at the scene post point;
  resolves (depth too) write at COLOR_ATTACHMENT_OUTPUT, so a memory barrier follows (the GUI pass loads them with
  no layout change: no Transition barrier). 2
  frames in flight; per-frame upload cmd buffer submitted first + global barrier; transient ring (verts, uniforms,
  staging).
- Resources made on the render thread at first use (textures/prim buffers created on loader threads, CPU copies).
  Changed prim buffer / texture -> new GPU object, old one `Retire`d (serial-based; any thread). Pipelines keyed by
  program/blend/colour mask/fill/a2c/samples/topology class; the rest is dynamic state.
- Post (vkpost.cpp): pass images bottom-up like GL's (frame blitted in flipped, uDepth via flipped R32F copy, last
  pass draws with flipped viewport) so vUV/gl_FragCoord/ViewPosition match; show_depth identical to GL.
- SPIR-V post passes (`*.spv` in post.ini, shaders/README.md): `BuildSpirvProgram` (vkpost.cpp) loads them as they
  are with `s_spirvVertexSource` (vUV location 0), fixed block offsets (`s_spirvOffsets`, 160 bytes), sampler slots =
  binding - 1 from the module's OpDecorate Binding; pragmas from the `.frag` beside it. glpost.cpp skips them.
  `port/spirv.py` compiles via `shar_spirv_compile` (libshar_glslang: plain Vulkan rules, no relaxed/shifts).
- SPIR-V materials (`*.spv` in materials.ini): glcustom calls `pglCustomTarget::BuildCustomSpirv` (default: skip, GL).
  vkpipeline `BuildCustomSpirv` compiles the game's CUSTOM program with a standard ShadeMaterial, then `FitSpirv`
  patches the user's fragment by OpName/OpMemberName: input Location <- vertex output of that name, block member
  Offset <- `p.offset[u]`, sampler Binding <- `p.binding[]`, set 0. `shaders/materials/vulkan/shar_material.glsl` =
  pglFragmentPrelude (REFLECTION, CUSTOM) + FinishFragment as main: keep in step with glshaders.cpp.
- 32-bit: non-dispatchable handles are all uint64_t (no overloads on handle type).


## Sun prototype (branch `vulkan-sun`, `vksun.cpp`)
- Off by default: Options > Graphics Shadows Off/Low/High (ini `shadows=off|low|high`, old `yes` = high), F9 cycles
  (saved like the menu). Device made with ray queries + buffer device address where it has them, unless `SHAR_SUN=0`
  (so the menu applies at once) or `SHAR_SUN_RT=0` (Low only, as an iGPU: tests the no-ray-query programs on an RTX).
  `SHAR_SUN=1` forces High, `SHAR_SUN=low` Low. High without ray queries runs Low. `SHAR_SUN=0`: programs and layout
  as before. Off in interiors and the front end whatever the setting (`RenderManager::ContextUpdate` ->
  `pvkSunSetSuppressed` each frame; blobs come back). `SHAR_SUN_SHADOW/LIGHT/LIT_SHADOW` levels, `SHAR_SUN_DIR=x,y,z`, `SHAR_SUN_ELEVATION=deg`
  (every level), `SHAR_SUN_SOFT=deg` (sun disc radius, default 0.3; 0 = hard), `SHAR_SUN_RAYS` (default 8),
  `SHAR_SUN_LOG=1` (2: every caster named as built: id, material, file, size), `SHAR_SUN_DEBUG=1` (one ray;
  shadowed pixels coloured by the blocker's id: red = low byte, green = high, blue 255).
- Compass direction: the sky's painted sun = the level's lens flare (`LensFlareDSG::SetCompositeDrawable`, its
  visibility mesh's joint, camera-relative -> `pvkSunSetSkyDirection`; the PC never draws the flare); L7 (night, no
  flare): the sky's `MoonShape` element (`WorldSphereDSG::SetCompositeDrawable`, UID match: release strips names),
  0,0.343,0.940, 20 deg up; L6: the billboard group `sunset_bb_bbgShape` (shader `l6_background_L6_moon_m`), -0.985,0.172,0.018, 9.9 deg up. Neither: the game's key light (L7's was ~85 deg off the moon). L1 sky sun 0.557,0.245,0.794 (in front of the Simpsons' house, 14 deg up); the key light is ~85
  deg round from it (behind the house), ~30 deg up. Height per level: `kLevelElevation` (measured: L1 14.2 sun, raised to 35; L2/L5 30 key light; L3 1.7 sunset sun; L4 19.4, L6 9.9, L7 20 moons; 0 = as is);
  level from `GameplayManager::SetLevelIndex` (`pvkSunSetLevel`). `SHAR_SUN_LOG=1` prints the sky's sun.
- Receivers: depth-writing, blend none or alpha (characters fade near the camera: always alpha-blended). Lit draws
  trace from every surface with ray mask 0x01 (world only; lit instances are 0x02): no self-shadow on top of the
  game's own shading.
- World space from p3d `tContext::LoadViewMatrix` (`pvkSetViewMatrix`); sun = strongest directional light the
  pipeline set (eye -> world). Opaque, depth-writing, perspective draws: prim buffer -> BLAS (CPU copy, built in the
  upload cmd), draw -> TLAS instance. Unlit (pre-lit world) instances with the same transform 3 frames running stay
  10 s out of view; lit ones (cars) only this frame (else parked cars at load left shadows behind). TLAS per frame
  in flight, built at Present before the upload barrier. Casters > 1500 m (sky) skipped, and those drawn at the
  camera (a 19 m ring moved with it: ring shadow round the player). Buffers rebuilt every frame (CPU-skinned
  characters: Lock each frame) rebuild their BLAS (fast-build) within 96 builds a frame, old shape meanwhile.
  Also skipped: casters > 8 m keeping the same offset from a moving camera, and draws whose projection is not the
  one set with the camera (`pvkSunInView`). A ring shadow that followed the player (user report) went away with
  these; neither check was seen to catch anything, cause unconfirmed: SHAR_SUN_DEBUG names it if it returns.
- Alpha-tested casters (foliage): non-opaque BLAS; custom index = row in the caster table (binding 6: UV/triangle
  offsets, mask | compare << 16, ref); 64x64 alpha masks from the texture's CPU copy (`pvkTexture::AlphaMask`,
  binding 7); UVs + triangles in a shared first-fit buffer (binding 8). Shader confirms candidates passing the test.
- Shaders: built-ins get `#version 460` + GL_EXT_ray_query + PVK_SUN (SPIR-V 1.4 target switched on in
  shar_glslang by the header); binding 5 = TLAS. Storage blocks: shar_glslang reflects with
  EShReflectionSeparateBuffers (else an SSBO is taken as the uniform block: block too small, heap overrun).
  Normals: smooth ones made at upload for buffers without (bound for unlit simple-program draws only): per vertex
  only the faces at its position within ~37 deg of its own, either winding (`pvkSunSmoothNormals`; averaging all
  bent the Simpsons' front wall towards its corners: dark wedges mistaken for shadows); shader takes the polygon
  normal at creases (< 0.75: a 90 deg corner's mean is 0.707).
- Cost (RTX 5060, 2560x1600 8x MSAA, L1 start): ~7 ms per full-screen ray per pixel; overdraw (alpha test stops
  early-z) multiplies it. Quad sharing (`pvkCore::quadFragment`, `PVK_SUN_QUAD`; `SHAR_SUN_QUAD=0` off): each pixel
  of a 2x2 quad traces every 4th spiral ray, sums shared by `subgroupQuadSwap*` (outside branches: helpers trace
  too); 1 ray = per pixel. Tried and dropped: 4 probes shared by the quad, early-out when they agree: faint
  penumbrae read fully lit for whole quads = regular bright dots. Fallback path (no quads): 3 probes (centre + 2
  rim) per pixel, early-out when they agree. Rays fall from full at `SHAR_SUN_NEAR` (80 m) to 1 at twice it; shadow
  fades out 0.8-1x `SHAR_SUN_FAR` (320 m, 0 = never); rays `SHAR_SUN_REACH` (150 m) long. Defaults 40 -> 12 ms,
  SOFT=4 RAYS=12 79 -> 22 ms, all-penumbra low sun 75 -> 18 ms.
- Baked map (`SHAR_SUN_BAKE`, on; `=0` = every ray per pixel through the whole scene, the comparison): the static
  world's shadow as heights towards the sun (`dot(p, sun)`) of the first surface per texel, on 4 levels of 2048^2
  texels on the plane facing the sun (`s_basis`, the shaders' disc axes), level 0 texel `SHAR_SUN_TEXEL` (0.04 m),
  doubling. One float storage buffer (binding 9, 64 MB), toroidal (`& 2047`), windows round the camera moved when an
  eighth off (`MapPlace`: the strips that came in are baked; `valid` = what draws may read, shrunk before the move).
  Baked by a compute shader (`kBakeSource`, `shar_spirv_compile` stage 2): a ray down along -sun per texel through
  the world's TLAS (alpha-tested casters as the draws'), budget `SHAR_SUN_BAKE_BUDGET` texels/frame (4M), in the
  upload cmd after the TLAS builds. Two TLAS per frame: draws' (lit, moving, not-yet-baked) and world's (built only
  in frames that bake). World = pre-lit, same transform `kWorldStreak` (30) frames, structure built <= 3 times;
  kept 10 min unseen, dropped when in view < 60 m and not drawn 30 frames (knocked down). Came/went/rebuilt: the
  64x64 tiles under its bounding sphere are dirty (`MarkDirty`, only where `valid`); a caster stays in the draws'
  TLAS until no dirty tile is left (ticket). Load: one full rebake (12.5M texels, ~4 frames); standing: ~0.
- Map sampling (`SunMapShade`, glshaders.cpp): finest level whose valid window holds p with a margin of the widest
  penumbra; p pushed a texel along the face. Receiver-plane bias: every tap compares with the face's plane where it
  lands (`slope`, per texel; without it sloped ground counted itself as blockers). 9-tap blocker search (disc of a
  blocker 40 m up, >= 2 texels), penumbra = mean blocker height x tan(sun radius), 2x`SHAR_SUN_RAYS` (8-32)
  bilinear taps. Refine: penumbra < 2 texels and taps disagree (a crisp edge: texel steps, user report at SOFT=0.3),
  or no level holds p: the pixel's rays (whole 2x2 quad) also trace the world's TLAS (binding 10, built every frame)
  and replace the map. Else the map times the rays' (draws' TLAS) result.
- Cost baked (same view, 2560x1600 8x): sun off ~1.5 ms, baked ~5.6 ms (before refine 3.6), SOFT=0.3 RAYS=16
  ~10 ms (RAYS=8 6.5: rays at every crisp edge), all rays ~18 ms. Texel size barely changes the cost. Truncated: outer penumbrae of blockers > 40 m up (search disc); things gone out of view keep their
  shadow until they are looked at (or 10 min).
- Blob shadows off while `pvkSunOn()` (F9 brings them back): characters/cars (`CharacterRenderable::DisplayShadow`,
  `GeometryVehicle::DisplayShadow`; not in the bonus game, its coloured blobs mark players), props/trees/lamps
  (`DisplaySimpleShadow` of StaticPhysDSG, StatePropDSG, InstStatEntityDSG), the large trees' decals painted into the
  world meshes (materials named *tree*shad*: `treeshadow_m`, `deadtree_shadow_m`, ...; `pvkMat::BakedTreeShadow`,
  skipped in `pvkContext::DrawPrimBuffer`). Kept: shadow-flagged world geometry
  (`StaticEntityDSG` IS_SHADOW: a few ~100 m chunks, rarely drawn).
- Gaps: custom material shaders get no sun, additive-blended foliage never casts.
- Map window follows `s_worldEye`, the camera of the last caster drawn: the frame's last LoadViewMatrix is the HUD
  radar's (150 m up), which put level 0 off the player (coarse levels, faint shadows; High hid it under rays).

## Low shadows (`Mode() == 1`, any Vulkan device)
- Same map, strips, dirty tiles and SunMapShade; no structures, no rays. Casters: pre-lit (`keep`) only, as host-visible
  meshes (`Blas::mesh`: positions, UVs if alpha-tested, uint32 triangles; `BuildMesh`). Lit things (cars, characters)
  and StatePropDSG keep the game's blobs (`pvkSunRaysOn`); StaticPhys/InstStat blobs off in both (they are in the map).
- Bake = `RasterRows`: chunk of <= 2048x512 texels, orthographic depth pass (D32, `s_rasterDepth`) of the world
  instances whose footprint touches it (depth = (top - h) / 4000, the ray bake's), alpha-tested through the masks;
  compute copy depth -> heights (`kCopySource`). Each chunk costs >= 1/16 of `SHAR_SUN_BAKE_BUDGET`. Matches the ray
  bake texel for texel (dumped both). Baked after `kLowWorldStreak` (8) steady frames: nothing casts before that.
- Programs: `PVK_SUN_RAYS` only with ray queries (else `#version 460` + PVK_SUN, layout without bindings 5/10);
  `uSunMapInfo.w = 1` = map alone (Low on a ray-query device: scenes built with 0 instances; a count > 0 builds
  from the instance buffers' stale rows = retired BLAS addresses after High: device lost). Taps 8 (`soft[1] = 4`).
  `SHAR_SUN_DEBUG=1` in Low: the map's shade, red where High would trace.
- Low <-> High: every caster released and rebuilt in the other form, map from scratch (`s_casterMode`).
- Cost (RTX 5060, 2560x1600 8x, L1 start, uncapped): off 1.65 ms, Low 2.1, High 6.5. Mesa lavapipe draws it.
- Edges: map only (1.5-texel filter at level 0 = 6 cm); small props near the ground slightly softer than High.

## Sun Defaults
The sun is off by default (Options > Graphics Shadows Low/High, or SHAR_SUN=low/1). Low ignores the ray settings
(SOFT still sets the penumbra width from the map). The defaults are SHAR_SUN_SOFT=0.3, SHAR_SUN_RAYS=8,
SHAR_SUN_NEAR=80, SHAR_SUN_FAR=320, SHAR_SUN_BAKE_BUDGET=4000000 and SHAR_SUN_SHADOW=0.68. These presets are good for comparing:

Defaults (crisp, like a real sun):
- port/play.sh --data DIR

Cheapest:
- SHAR_SUN_RAYS=2 SHAR_SUN_NEAR=40 SHAR_SUN_FAR=250 port/play.sh --data DIR

The previous soft defaults:
- SHAR_SUN_SOFT=2 SHAR_SUN_RAYS=6 SHAR_SUN_NEAR=40 SHAR_SUN_FAR=250 port/play.sh --data DIR

Very soft (needs more rays or the edges look grainy):
- SHAR_SUN_SOFT=4 SHAR_SUN_RAYS=12 port/play.sh --data DIR

Faint shadows, closer to the original look:
- SHAR_SUN_SHADOW=0.8 SHAR_SUN_LIT_SHADOW=0.82 port/play.sh --data DIR

Exaggerated, to judge the shapes:
- SHAR_SUN_SOFT=0 SHAR_SUN_SHADOW=0.3 SHAR_SUN_LIT_SHADOW=0.4 port/play.sh --data DIR

What each setting does:
- SHAR_SUN_SOFT: how blurred the edges are, in degrees.
  - 0 gives hard edges. 0.3 is the default, about what the real sun gives. 2 is soft, and 4 or more gets very soft.
  - The blur grows with distance from the caster, so the bottom of a pole stays sharp and the top's shadow goes fuzzy.
- SHAR_SUN_RAYS: how smooth those blurred edges look.
  - Few rays give a speckled edge, more give a smooth one.
  - A rough guide is at least 3 rays per degree of softness, so 6 for 2° and 12 for 4°; 8 is the default.
  - Ignored when SOFT=0. Maximum 32.
  - Each 2x2 block of pixels shares its rays, so the cost per pixel is about a quarter of this. 12 is fine on your RTX 5060.
- SHAR_SUN_SHADOW: how bright the world is in shade.
  - 1.0 means no shadow at all, 0.68 is the default, and 0.3 is very dark.
- SHAR_SUN_LIT_SHADOW: the same for characters and cars, default 0.7.
  - Keep it near SHAR_SUN_SHADOW, or Homer will look brighter or darker than the ground he's standing on.
- SHAR_SUN_LIGHT: how bright sunlit ground gets, default 1.22. Lower it toward 1.0 if sunlit areas look washed out.
- SHAR_SUN_NEAR: rays are at full count up to this distance (default 80 m), dropping to 1 at twice it.
- SHAR_SUN_FAR: shadows fade out towards this distance and no rays are traced past it (default 320 m, 0 = never).
- SHAR_SUN_REACH: how far a ray looks for a caster (default 150 m). Raise it if tall buildings lose far shadows at a low sun.
- SHAR_SUN_QUAD=0: turns off the 2x2 ray sharing (for comparing; slower).
- SHAR_SUN_BAKE=0: traces every shadow per pixel, as before the baked map (for comparing; about 5x slower).
- SHAR_SUN_TEXEL: the baked map's finest texel, default 0.04 m. Crisp edges are traced with rays whatever the texel; smaller only shrinks the baked area (0.01: about +-80 m, rays beyond).
- With a crisp sun (SOFT 0.3-0.5) every shadow edge is ray traced: RAYS=6-8 is enough and much cheaper than 16.

Tips:
- Press F9 to step off, low, high in the same spot. That's the easiest way to judge any of these.
- On L1 the sun comes from where it is painted in the sky, in front of the Simpsons' house, at 35 degrees. SHAR_SUN_ELEVATION=14 puts it exactly where the sky shows it (long shadows); 68 is a midday sun (short shadows, mostly under things, softness harder to see).
- Good spots to look: the lamp posts and trees along the street, and Homer walking in and out of the house's shadow.
