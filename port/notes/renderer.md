# Renderer (GL pddi `libs/pure3d/pddi/gl/`, Vulkan pddi `libs/pure3d/pddi/vk/`)

GL pddi was never finished against D3D8 (shipped game used pddidx8): when something renders wrong compare
`libs/pure3d/pddi/dx8` in the dump.

## Pipeline (glpipeline.cpp, GL 3.3 core)
- Fixed-function state kept on CPU (matrices, 8 lights, fog, material, current colour/normal/uv); one GLSL program per
  material kind (`pglProgramType`, shared sources + `#define`s, each tracks state versions). Uniforms re-sent only on
  change (same material set ~250x/frame). `display_linux/gl.hpp` includes glcorearb.h: removed GL calls don't compile.
- Prim buffers upload lazily in `Display` (loader threads fill them); glBegin-style streams via ring buffer. Prim
  buffers draw the set index count, not the allocated (uninitialised indices crashed the driver).
- Lighting follows D3D8: normals renormalised, local viewer, specular only with shininess > 0, point-light range,
  `d3dShader` defaults (black diffuse), two-sided materials disable culling (`SetMaterialTwoSided`),
  EMISSIVEALPHA = material alpha (fades), D3D8 blend table incl. subtract (shadow volumes). Linear fog on |eye z|.
- Shaders in retail data: simple, spheremap (car paint), environment. `gldev.cpp` maps names like
  `pddiBaseShader::InstallShader` (unknown -> simple). Reflection = `d3dReflShader` (envBlend set per frame by
  geometryvehicle.cpp). refract (PS2's) only for mods' RefractionShaderSupport (mods.md). Not ported (unused): lightmap, layered, projtex, pointsprite, toon, multi-CBV, flat shading.
- The game renders with culling OFF (renderflow.cpp) and art winding is inconsistent.
- Fixed D3D8 mismatches: `SetColourWrite` missing; glClear honours write masks, D3D Clear doesn't (`pglContext::Clear`
  opens them); `alphaRef` uninitialised + `PDDI_SP_ALPHACOMPARE_THRESHOLD` (default 0.5) -> fences vanished;
  GL_CLAMP -> GL_CLAMP_TO_EDGE (`glmat.cpp` uvTable; dark seams); `~pglTexture` leaked GL textures;
  `pglDisplay::SwapBuffers` must clear `reset` (else re-upload every frame).
- Gamma: GLSL pass before swap (`ApplyGamma`; SDL ramp does nothing on Wayland), skipped at 1.0.

## Textures
- Lock() views are bottom-up (negative pitch) except DXT (blocks copied as-is): decoders use `Builder::BOTTOM`.
- DXT only in sprites (front end/HUD/loading); uploaded compressed (`gltex.cpp`); software decode without S3TC or
  `SHAR_GL_NO_S3TC=1`. Uploads lazy on render thread (D3D8 multithreaded, GL not: worker shared contexts, see platform.md).
- Mipmaps: glGenerateMipmap for uncompressed; trilinear + anisotropy (`pglTexture::SetSampling`; ini `mipmaps=`,
  `anisotropy=`; runtime-changeable). Alpha-tested non-blended: alpha +25% per mip level (uMipAlpha) against thinning.
- GL_OUT_OF_MEMORY upload: reported once, retried.
- Power-of-two sizes only (D3D8 rule, `PDDI_TEX_NOT_POW_2`), except uncompressed `PDDI_USAGE_DYNAMIC` textures
  (mods' video textures): Lock() sizes from xSize/ySize.

## MSAA
- Offscreen MS FBO (RGBA8 + D24S8) bound in `BeginFrame` (`BeginFrameTarget`), blitted in `ResolveFrame` at the scene
  post point (GUI single-sampled) or at swap. FBO because simpsons.ini is read after the context exists
  (`LoadConfig` -> `pglSetAntialiasing`; `antialiasing=` default 4; `SHAR_MSAA`). Incomplete FBO -> off with message.
- Alpha-to-coverage for alpha-tested non-blended (GREATER/GEQUAL) while multisampled (`SetMultisampled`), alpha ramped
  over ~1 px via fwidth. `SHAR_ALPHA_TO_COVERAGE=0` off.

## Options > Graphics (`CGuiScreenGraphics`, front end + pause)
- Anti-aliasing Off/2/4/8, Shaders (`pglSetUserShaders`, ini `shaders=`), Renderer (ini `renderer=opengl|vulkan`;
  next start). Apply at once; config saved on leaving.
- No page of its own: reuses the Display page, `Scrooby::Text::Clone` (Linux port) copies rows' label/value texts,
  originals hidden; arrows shared (transform saved around AddMenuItem, which scales them). Built before
  CGuiScreenDisplay (clones copy colour: must be unhighlighted); Display's InitIntro calls `ShowPage(false)`.
- Options "Graphics" row: `AddGraphicsMenuItem` clones "Display", moves rows with SetPosition (CGuiMenu resets item
  transforms every frame, so Translate is lost).

## User shaders (format: shaders/README.md). M toggles all (`pglToggleUserShaders`; generation bump)
- Post (`glpost.cpp`, `shaders/post.ini`): [scene] passes in `RenderManager` before the GUI layer (only if a lower
  layer drew and no FMV), [final] in `SwapBuffers` before gamma. Ping-pong FBOs; depth copied only if a pass uses
  uDepth (depth 0-1: game never calls SetZRange). `pglScreenPassState` saves/restores touched state incl. tex units
  0-2. Reload on mtime change. Pragmas: `shar_original` (input = uOriginal), `shar_scale <f>` (own smaller target, not
  last pass), `shar_texture <sampler> <file.png>` (units 3+, bundled libpng 1.0.3: png.h before setjmp.h).
  Uniforms incl. uProjection/uInverseProjection, ViewPosition().
- SSAO: half size, 8 samples, 4x4 Bayer rotations, log distance in G/B; apply = exact 4x4 box (5x5 weighted taps),
  depth-weighted. 0.27 ms at 1080p (RTX 5060). Tonemap: LDR widen x/(1-0.6x) + ACES/Hable/Reinhard; LUTs ReShade 1024x32.
- Materials (`glcustom.cpp`, `shaders/materials.ini`): [types] (pddi shader name, `pglMat::SetTypeName`), [files]
  (chunk file name, '/'-separated, case as opened), [materials] (tShader name via RAD_LINUX hook in p3d/shader.cpp ->
  `pglSetMaterialName`); fnmatch -> GLSL defining `vec4 ShadeMaterial()`. User rules, then each mod's `Shaders/`.
  Up to 32 program slots (`BuildCustomProgram`); prelude: StandardColour, BaseTexture, LightVector, FinishFragment.
  Up to 4 passes per rule (`GetPasses`; DrawPrimBuffer loops as D3D8). `MaterialVertex` -> vertex stage.
  `#pragma shar_cull none`. outline.glsl keeps shells by normal (winding unreliable). World mostly unlit, no normals.
- Test keys: `SHAR_KEYPRESS_SCRIPT="M@ms"` (SDL events; SHAR_KEY_SCRIPT feeds game input).

## Aspect ratio / resolution
- `p3d/displayaspect.hpp`: 4:3 cameras render Hor+ (Vert+ when narrower) via effective values in `tCamera`; `GetFOV`
  still returns 4:3 (cutscenes read it); culling uses `GetEffectiveFOV`. Scrooby UI in centred 4:3, clipped;
  full-width polys stretch; full-page `FePure3dObject` fills; TV frame `Sprite::SetStretchToWindow`.
- Resolution enum indexes a runtime table (`linuxplatform_sdl.cpp`): enum's 10, common sizes, desktop, ini WxH.
  `BuildResolutionList` shows those fitting the desktop. `display=borderless|fullscreen|window`; exclusive fullscreen
  sets closest mode (leave fullscreen first or XWayland keeps size).

## Diagnostics
- `SHAR_GL_DEBUG=1`, `SHAR_GL_STATS=1` (draws/uploads, CPU/GPU ms per 5 s), `SHAR_MATERIAL_LOG=1`.
- Sun lighting on world: dropped (pre-lit world has no normals; needs shadow maps).

## Vulkan (`pddi/vk/`, ini `renderer=vulkan`, `SHAR_RENDERER=vulkan|opengl` overrides)
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
  via empty rendering instance with resolve attachments (depth/stencil SAMPLE_ZERO) at the scene post point. 2
  frames in flight; per-frame upload cmd buffer submitted first + global barrier; transient ring (verts, uniforms,
  staging).
- Resources made on the render thread at first use (textures/prim buffers created on loader threads, CPU copies).
  Changed prim buffer / texture -> new GPU object, old one `Retire`d (serial-based; any thread). Pipelines keyed by
  program/blend/colour mask/fill/a2c/samples/topology class; the rest is dynamic state.
- Post (vkpost.cpp): pass images bottom-up like GL's (frame blitted in flipped, uDepth via flipped R32F copy, last
  pass draws with flipped viewport) so vUV/gl_FragCoord/ViewPosition match; show_depth identical to GL.
- 32-bit: non-dispatchable handles are all uint64_t (no overloads on handle type).
- Not yet: pipeline cache on disk, Screenshot(), GPU times in SHAR_GL_STATS (prints draws/pipelines only).
