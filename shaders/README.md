# Shaders

Two kinds of GLSL shaders of your own, both plain text files you can edit while the game runs (changes show up
within a second):

- **Post-processing** (`post.ini`, `post/`): passes over the picture each frame: anti-aliasing, bloom, colour
  grading, a CRT look, ...
- **Material shaders** (`materials.ini`, `materials/`): replace how chosen materials are drawn: toon shading, car
  paint, ... (see [Material shaders](#material-shaders) below)

Press **M** in the game to turn all of them off and on again (for comparing).

A shader that does not compile is reported on the console with its line numbers (in the log,
`run/logs/last-session.log`, when started by play.sh) and the game carries on without it until it builds again.

# Post-processing shaders

## Where they go

The game reads the `shaders/` folder in its working directory (or the folder named by the `SHAR_SHADERS`
environment variable). `port/play.sh` sets it up: your game folder's `shaders/` if it has one, otherwise a copy of
this folder in `run/shaders/` (edit that copy, not this one). `SHAR_POST=0` turns post-processing off.

## Choosing passes: post.ini

```ini
[scene]                  # see the 3D world, before the HUD and menus are drawn on top
post/bloom_bright.glsl
post/bloom_blur_h.glsl
post/bloom_blur_v.glsl
post/bloom_add.glsl
post/fxaa.glsl

[final]                  # see the whole frame, HUD and menus included
post/crt.glsl
```

Each line names a shader file, relative to the shaders folder. Passes run top to bottom, each one reading the
previous one's output. `#` or `;` starts a comment. Up to 16 passes per stage. The game's gamma setting is applied
after everything. The scene stage only runs when the 3D world was drawn (not in the front-end menus).

## Writing a pass

A pass is the body of a GLSL 3.30 fragment shader, without a `#version` line. The game puts this in front of it:

```glsl
in vec2 vUV;                    // position on the screen: (0,0) bottom left, (1,1) top right
out vec4 fragColour;            // write the pixel's colour here
uniform sampler2D uScene;       // the picture so far (the previous pass's output)
uniform sampler2D uOriginal;    // the picture as this stage began (before its first pass)
uniform sampler2D uDepth;       // the depth buffer (0 near .. 1 far, not linear)
uniform vec2 uResolution;       // the picture's size in pixels
uniform float uTime;            // seconds since the game started
uniform int uFrame;             // frame counter
uniform float uNear, uFar;      // the camera's near and far planes (0 when the last view was not 3D)
uniform mat4 uProjection;       // the camera's projection (eye space to clip space), and its inverse
uniform mat4 uInverseProjection;
float LinearDepth(vec2 uv);     // distance from the camera at uv, in game units (about metres)
vec3 ViewPosition(vec2 uv);     // the eye-space position of what is drawn at uv (camera at 0,0,0 looking down -z)
```

The smallest pass:

```glsl
void main()
{
    vec3 c = texture(uScene, vUV).rgb;
    fragColour = vec4(1.0 - c, 1.0);        // negative image
}
```

Settings are best written as `const float NAME = value;` at the top of the file (the examples do that).

A pass can use images: `#pragma shar_texture uName file.png` (up to four per pass) declares `uniform sampler2D
uName` holding the PNG, the path relative to the pass's own file; the image's top row is at `v = 0`. Images reload
when they change too.

An effect of several passes that needs the picture as it was before it began (bloom adds its glow to it, ambient
occlusion darkens it) puts `#pragma shar_original` in its first pass: from that pass on, `uOriginal` is that pass's
input. Without it, `uOriginal` is the picture as the stage began.

A pass that does not need every pixel (ambient occlusion, blurs) can work at a smaller size: `#pragma shar_scale 0.5`
draws it at half the width and height, a quarter of the cost. The next pass reads its output through `uScene` as
usual (`textureSize(uScene, 0)` gives its size), and `uResolution` is the size the pass draws at. The last pass of a
stage always works at full size.

`uDepth` is copied only when a pass uses it. In the final stage it also holds what the HUD and menus drew.

## The examples (post/)

| File | Stage | What it does |
|------|-------|--------------|
| `ssao.glsl`, `ssao_blur_h.glsl`, `ssao_blur_v.glsl` | scene | ambient occlusion: darkens corners, recesses and the ground around objects (three passes, in this order: the occlusion, then an edge-aware blur that also darkens the picture; `SHOW_AO` in `ssao_blur_v.glsl` shows it alone; works at full size, `#pragma shar_scale` in `ssao.glsl`: 0.5 is a quarter of the cost) |
| `tonemap.glsl` | scene | filmic tone mapping (`CURVE`: ACES, Hable, Reinhard): softer highlights, richer midtones; mid grey stays put; `SATURATION` sets how much of the curve's colour boost to keep |
| `grade.glsl` | scene | colour grading: white balance, exposure, contrast, saturation, vibrance, lift / gamma / gain, split toning |
| `lut.glsl` | scene | colour grading with a lookup-table image (`lut/`: `neutral`, `warm`, `teal_orange`, `cool`); see below |
| `fxaa.glsl` | scene | anti-aliasing: smooths jagged edges |
| `sharpen.glsl` | scene | contrast-adaptive sharpening |
| `vignette.glsl` | scene | darker corners |
| `bloom_bright.glsl`, `bloom_blur_h.glsl`, `bloom_blur_v.glsl`, `bloom_add.glsl` | scene | glow around bright things (four passes, in this order) |
| `crt.glsl` | final | old TV: curved screen, scanlines, shadow mask |
| `show_depth.glsl` | scene | shows the depth buffer (near black, far white) |

### Making your own LUT

1. Take a screenshot of the game and paste `post/lut/neutral.png` (1024 x 32) onto it, anywhere.
2. Grade the whole image in GIMP, Photoshop, Krita, Resolve...: colour adjustments only (curves, levels, hue /
   saturation, colour balance, selective colour); nothing that blurs, sharpens or moves pixels.
3. Cut the strip back out at exactly 1024 x 32, save it as a PNG in `post/lut/`, and name it in `lut.glsl`'s
   `#pragma shar_texture` line. The game now grades every frame the way you graded the screenshot.

Cost: all of them together take about 0.25 ms a frame at 1280x720 on a current graphics card; ambient occlusion
takes up to about 1 ms at 2560x1440.

# Material shaders

A material shader takes over the colour of the materials it is assigned to. The game still does the rest: it
positions and lights the vertices as before, and fog, the alpha test (see-through parts) and anti-aliasing are
applied to what the shader returns.

## Assigning them: materials.ini

```ini
[types]                     # by the shader type the game's art names
spheremap = materials/carpaint.glsl
[files]                     # by the art file the material comes from (wins over [types])
*chars/* = materials/flat.glsl
[materials]                 # by material name (wins over [files] and [types])
homer_v* = materials/toon.glsl
```

Each line is `pattern = file`. Patterns may use `*` and `?` and ignore case; the file is relative to this folder.
Within each section the first matching line wins. Up to four files, comma-separated, draw the material once with
each, in order: `*chars/* = materials/flat.glsl, materials/outline.glsl` is flat colours, then outlines.

- **Types**: `simple` is almost everything, including the HUD and menus; `spheremap` and `environment` are car
  paint and chrome. A mod's art can name a type of its own (`toon`, say), which draws as `simple` unless a rule
  gives it a shader.
- **Files** are the art files materials are loaded from, as the game opens them (`art/chars/homer_m.p3d`; the
  case varies, matching ignores it): `*chars/*` is every character, `*cars/*` every car, `*cars/famil_v*` one car.
- **Material names** come from the art. Start the game with `SHAR_MATERIAL_LOG=1` to print each material's name,
  type and file the first time it is drawn.

**Mods** can ship material shaders: a `Shaders/` folder in the mod's root folder holding a `materials.ini` and the
shader files it names, next to it (no subfolders). Your own `materials.ini` comes first, so its rules win over a
mod's.

## Writing one

A material shader is the body of a GLSL 3.30 fragment shader, without a `#version` line, that defines
`vec4 ShadeMaterial()` and returns the colour (alpha included). The smallest one draws everything as the game would:

```glsl
vec4 ShadeMaterial()
{
    return StandardColour();
}
```

Available to it:

```glsl
vec4 StandardColour();          // the material as the game draws it (lit colour x texture + reflection + highlight)
vec4 BaseTexture();             // the material's texture at this pixel (white without one)
vec3 LightVector(int i, out float attenuation);  // towards light i from this pixel, eye space; attenuation 0 out of range
vec3 Lighting(vec3 n, float wrap, out vec3 specular);  // the game's lighting here for normal n (eye space): returns
                                // the light colour (multiply by BaseTexture()), the game's highlights in specular;
                                // wrap > 0 softens the fall-off ((n.l + wrap) / (1 + wrap))

in vec4 vColour;                // the lit colour (unlit materials: their vertex colours, which hold baked lighting)
in vec3 vSpecular;              // the game's highlight
in vec2 vUV;                    // texture coordinates
in vec2 vReflectionUV;          // reflection map coordinates (car paint)
in vec3 vNormal;                // the normal, eye space (x right, y up, z towards the camera)
in vec3 vEyePosition;           // the pixel's position, eye space (the camera at 0,0,0)

uniform sampler2D uTexture;            uniform bool uTextured;
uniform sampler2D uReflection;         uniform bool uReflectionTextured;   uniform vec4 uEnvBlend;
uniform bool uLit;                     // the material is lit by the game's lights
uniform vec4 uMatAmbient, uMatDiffuse, uMatSpecular, uMatEmissive;   uniform float uShininess;
uniform vec4 uGlobalAmbient;
uniform int uLightCount;               uniform vec3 uLightColour[8];   // the lights in use
uniform float uTime;                   // seconds since the game started
```

(`gl_FrontFacing` is no use for telling the outside of a model from the inside: the game draws with culling off
and its models do not all wind their polygons the same way. Compare `vNormal` with `-vEyePosition` instead, as
`outline.glsl` does.)

### Moving vertices

A shader can also move the model's vertices, by defining `vec4 MaterialVertex(vec3 position, vec3 normal)`: it gets
a vertex's position and normal (model space) and returns where it goes on screen (clip space, as `gl_Position`).
The file is then compiled twice, once for each stage, so keep each part inside its stage's `#ifdef`:

```glsl
#ifdef VERTEX_STAGE
vec4 MaterialVertex(vec3 position, vec3 normal)
{
    return StandardPosition(position + normal * 0.05);     // a little fatter
}
#endif

#ifdef FRAGMENT_STAGE
vec4 ShadeMaterial()
{
    return StandardColour();
}
#endif
```

In the vertex stage: `StandardPosition(position)` (where the game puts it), `uModelView`, `uProjection`,
`uNormalMatrix` (model to eye space, for normals), `uResolution` (the window in pixels), `uTime`. Lighting is still
worked out at the vertex's original place.

`#pragma shar_cull none` in a file draws every face of the model with it, even where the game would cull some.

Most of the world is unlit: its lighting is painted into the vertex colours and it has no normals (`vNormal` is
then the same across each object), so lighting-based effects work on lit materials (characters, cars). `StandardColour()`
and `vColour` are the way to include the world's own shading.

## The examples (materials/)

| File | For | What it does |
|------|-----|--------------|
| `outline.glsl` | after another, `[files] *chars/* = materials/flat.glsl, materials/outline.glsl` | black cartoon outlines (inverted hull: the model again, pushed out along its normals, back half only), a fixed width in pixels |
| `standard.glsl` | first of several | draws a material exactly as the game does (to add an outline to the normal look) |
| `flat.glsl` | `[files] *chars/*` | characters in flat cartoon colours: no shading across the model, still as bright or dark as the scene; `FLATNESS` blends towards the game's smooth shading (1 flat, 0 smooth) |
| `pixellit.glsl` | `simple`, `spheremap`, `environment` | per-pixel lighting: the game's lighting computed for every pixel (smooth shading on low-poly characters and cars); optional sheen (`HIGHLIGHT`) and rim light (`RIM`) |
| `softlit.glsl` | `[files] *chars/*` | soft per-pixel lighting for low-polygon characters: wrapped light (`WRAP`) and normals bent towards the camera (`SMOOTHING`) hide the facets; as bright as the game's; optional rim light (`RIM`) in the scene's light colour |
| `toon.glsl` | `simple` | cel shading: light in flat bands (per-pixel on lit materials; colours banded on the rest) |
| `carpaint.glsl` | `spheremap`, `environment` | the car paint plus a sharp highlight per light and stronger reflections at grazing angles |
| `normals.glsl` | anything | shows the normals as colours |

They cost nothing measurable on a current graphics card.
