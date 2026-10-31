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
- `SHAR_SUN=1` (device made with ray queries + buffer device address), F9 toggles. Off/unsupported: programs and
  layout as before. `SHAR_SUN_SHADOW/LIGHT/LIT_SHADOW` levels, `SHAR_SUN_DIR=x,y,z`, `SHAR_SUN_ELEVATION=deg`
  (every level), `SHAR_SUN_SOFT=deg` (sun disc radius, default 2; 0 = hard), `SHAR_SUN_RAYS` (default 6),
  `SHAR_SUN_LOG=1` (2: every caster named as built: id, material, file, size), `SHAR_SUN_DEBUG=1` (one ray;
  shadowed pixels coloured by the blocker's id: red = low byte, green = high, blue 255).
- Sun height per level: `kLevelElevation` (vksun.cpp; L1 68 deg = midday), the game light's compass direction kept;
  level from `GameplayManager::SetLevelIndex` (`pvkSunSetLevel`). The game's own key light is ~30 deg.
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
  Normals: smooth ones made at upload for buffers without (bound for unlit simple-program draws only), polygon
  normal at creases (< 0.7).
- Gaps: the game's blob shadow is still drawn under characters, custom material shaders get no sun,
  additive-blended foliage never casts.
