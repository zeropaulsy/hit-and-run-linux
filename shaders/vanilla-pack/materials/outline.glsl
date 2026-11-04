// Cartoon outlines (inverted hull): an extra pass draws the model again, pushed outwards along its normals, keeping
// only the parts whose normals point away from the camera, in the outline colour. The model covers the middle of
// that shell; around its edges the shell shows as a line. (The parts are told apart by their normals, not by which
// way the polygons wind: the game's art is not consistent about that.) Use it as a second pass after the normal
// drawing, e.g. in materials.ini:
//
//     [files]
//     *chars/* = materials/flat.glsl, materials/outline.glsl
//
// or, to keep the game's own look for the model itself: *chars/* = materials/standard.glsl, materials/outline.glsl

#pragma shar_cull none                 // every face of the shell (the fragment part picks the back ones)

const float WIDTH = 2;                  // line width in pixels at 1080 lines (scaled with the window height)
const float MIN_WIDTH = 0.6;            // far away, lines thin down to this share of WIDTH ...
const float FULL_WIDTH_DISTANCE = 6.0;  // ... beyond this distance (game units, about metres) from the camera
const vec3 COLOUR = vec3(0.0);          // line colour

#ifdef VERTEX_STAGE
vec4 MaterialVertex(vec3 position, vec3 normal)
{
    vec4 clip = StandardPosition(position);

    // the normal's direction on screen; the shell is pushed that way by a fixed number of pixels, so lines are as
    // wide near and far (thinning a little with distance) whatever the model's size
    vec3 eyeNormal = uNormalMatrix * normal;
    vec2 direction = (uProjection * vec4(eyeNormal, 0.0)).xy;
    float length2 = dot(direction, direction);
    if(length2 > 0.0)
    {
        float distanceScale = clamp(FULL_WIDTH_DISTANCE / max(clip.w, 1e-3), MIN_WIDTH, 1.0);
        float pixels = WIDTH * uResolution.y / 1080.0 * distanceScale;
        clip.xy += direction * inversesqrt(length2) * pixels * 2.0 / uResolution * clip.w;
    }
    return clip;
}
#endif

#ifdef FRAGMENT_STAGE
vec4 ShadeMaterial()
{
    // the front of the shell would cover the model: keep the parts facing away from the camera
    if(dot(vNormal, -vEyePosition) > 0.0)
        discard;
    // fading characters fade their outline too (the material's alpha)
    return vec4(COLOUR, uLit ? uMatDiffuse.a : vColour.a);
}
#endif
