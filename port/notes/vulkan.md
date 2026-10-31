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
- 32-bit: non-dispatchable handles are all uint64_t (no overloads on handle type).

